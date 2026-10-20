#include "ThemeHttp.hpp"

#include "core/DebugLog.hpp"

#include <curl/curl.h>
#include <curlpp/Easy.hpp>
#include <curlpp/Exception.hpp>
#include <curlpp/Infos.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/cURLpp.hpp>
#include <switch.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>

namespace {

// Transfers hold the shared lock and run concurrently; (re)initialising or
// shutting the runtime down takes the exclusive lock.
std::shared_mutex g_themeHttpMutex;
// Idle curl handles. Re-using a handle keeps the TLS connection to the host
// alive, which removes the handshake (several hundred ms on the Switch) from
// every request after the first.
std::mutex g_poolMutex;
std::vector<std::unique_ptr<curlpp::Easy>> g_easyPool;
constexpr std::size_t kMaxPooledHandles = 6;
bool g_nifmInitialized = false;
bool g_socketInitialized = false;
bool g_curlInitialized = false;

constexpr int kRequestAttemptCount = 2;

std::string resultToString(Result rc) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%08X", (unsigned int)rc);
    return buf;
}

bool runtimeInitializedLocked() {
    return g_nifmInitialized && g_socketInitialized && g_curlInitialized;
}

void drainEasyPool() {
    std::vector<std::unique_ptr<curlpp::Easy>> doomed;
    {
        std::lock_guard<std::mutex> lk(g_poolMutex);
        doomed.swap(g_easyPool);
    }
    doomed.clear();
}

void shutdownRuntimeLocked() {
    drainEasyPool();
    if (g_curlInitialized) {
        curl_global_cleanup();
        g_curlInitialized = false;
    }
    if (g_socketInitialized) {
        socketExit();
        g_socketInitialized = false;
    }
    if (g_nifmInitialized) {
        nifmExit();
        g_nifmInitialized = false;
    }
}

bool initializeRuntimeLocked() {
    if (runtimeInitializedLocked())
        return true;

    if (!g_nifmInitialized) {
        Result rc = nifmInitialize(NifmServiceType_User);
        if (R_FAILED(rc)) {
            DebugLog::log("[themeshop] nifmInitialize failed: %s", resultToString(rc).c_str());
            shutdownRuntimeLocked();
            return false;
        }
        g_nifmInitialized = true;
    }

    if (!g_socketInitialized) {
        Result rc = socketInitializeDefault();
        if (R_FAILED(rc)) {
            DebugLog::log("[themeshop] socketInitializeDefault failed: %s", resultToString(rc).c_str());
            shutdownRuntimeLocked();
            return false;
        }
        g_socketInitialized = true;
    }

    if (!g_curlInitialized) {
        CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (rc != CURLE_OK) {
            DebugLog::log("[themeshop] curl_global_init failed: %s", curl_easy_strerror(rc));
            shutdownRuntimeLocked();
            return false;
        }
        g_curlInitialized = true;
    }

    DebugLog::log("[themeshop] http runtime ready");
    return true;
}

void ensureInternetConnectionReady(const std::string& url) {
    NifmInternetConnectionStatus status = NifmInternetConnectionStatus_ConnectingUnknown1;
    u32 strength = 0;
    Result rc = nifmGetInternetConnectionStatus(nullptr, &strength, &status);
    if (R_FAILED(rc)) {
        throw std::runtime_error("nifmGetInternetConnectionStatus failed: " + resultToString(rc));
    }
    if (status != NifmInternetConnectionStatus_Connected) {
        throw std::runtime_error("Internet connection is not ready for " + url);
    }
}

bool isAbortRequested(const themeshop::http::AbortCheck& shouldAbort) {
    return shouldAbort && shouldAbort();
}

void configureRequest(curlpp::Easy& request,
                      const std::string& url,
                      std::vector<std::uint8_t>& response,
                      const std::list<std::string>& headers,
                      const themeshop::http::ProgressCallback& onProgress = {},
                      const themeshop::http::AbortCheck& shouldAbort = {}) {
    request.setOpt<curlpp::options::Url>(url);
    request.setOpt<curlpp::options::FollowLocation>(true);
    request.setOpt<curlpp::options::NoSignal>(true);
    request.setOpt<curlpp::options::ConnectTimeout>(12L);
    request.setOpt<curlpp::options::Timeout>(30L);
    request.setOpt<curlpp::options::IpResolve>((long)CURL_IPRESOLVE_V4);
    request.setOpt<curlpp::options::UserAgent>(std::string("SwitchU/") + SWITCHU_VERSION);
    if (!headers.empty()) {
        request.setOpt<curlpp::options::HttpHeader>(headers);
    }
    // Let curl negotiate gzip/deflate (it decodes transparently) - catalog
    // and manifest JSON shrink a lot. Empty string = every encoding built in.
    request.setOpt<curlpp::options::AcceptEncoding>("");
    request.setOpt<curlpp::options::WriteFunction>(
        [&response](char* ptr, size_t size, size_t nmemb) -> size_t {
            const size_t total = size * nmemb;
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(ptr);
            response.insert(response.end(), bytes, bytes + total);
            return total;
        });
    if (onProgress || shouldAbort) {
        request.setOpt<curlpp::options::NoProgress>(false);
        request.setOpt<curlpp::options::ProgressFunction>(
            [onProgress, shouldAbort](double downloadTotal, double downloaded, double, double) {
                if (isAbortRequested(shouldAbort))
                    return 1; // Non-zero aborts the curl transfer immediately.
                if (onProgress) {
                    onProgress(downloaded > 0.0 ? static_cast<std::uint64_t>(downloaded) : 0,
                               downloadTotal > 0.0 ? static_cast<std::uint64_t>(downloadTotal) : 0);
                }
                return 0;
            });
    }
}

std::unique_ptr<curlpp::Easy> acquireEasy() {
    {
        std::lock_guard<std::mutex> lk(g_poolMutex);
        if (!g_easyPool.empty()) {
            auto handle = std::move(g_easyPool.back());
            g_easyPool.pop_back();
            handle->reset(); // drops options, keeps live connections
            return handle;
        }
    }
    return std::make_unique<curlpp::Easy>();
}

void releaseEasy(std::unique_ptr<curlpp::Easy> handle) {
    if (!handle)
        return;
    std::lock_guard<std::mutex> lk(g_poolMutex);
    if (g_easyPool.size() < kMaxPooledHandles)
        g_easyPool.push_back(std::move(handle));
}

std::vector<std::uint8_t> performRequestBytes(const std::string& url,
                                              const std::list<std::string>& headers,
                                              const themeshop::http::ProgressCallback& onProgress,
                                              const themeshop::http::AbortCheck& shouldAbort) {
    if (isAbortRequested(shouldAbort))
        throw std::runtime_error("Cancelled");

    std::vector<std::uint8_t> response;
    auto request = acquireEasy();
    configureRequest(*request, url, response, headers, onProgress, shouldAbort);
    try {
        request->perform();
    } catch (const curlpp::LibcurlRuntimeError& ex) {
        if (isAbortRequested(shouldAbort))
            throw std::runtime_error("Cancelled");
        const char* detail = ex.what();
        std::string msg = "HTTPS failed";
        if (detail && detail[0] != '\0') {
            msg += ": ";
            msg += detail;
        } else {
            msg += ": ";
            msg += curl_easy_strerror(ex.whatCode());
        }
        throw std::runtime_error(msg);
    } catch (const std::exception& ex) {
        if (isAbortRequested(shouldAbort))
            throw std::runtime_error("Cancelled");
        const char* detail = ex.what();
        if (detail && detail[0] != '\0')
            throw std::runtime_error(detail);
        throw std::runtime_error("HTTPS request failed");
    } catch (...) {
        if (isAbortRequested(shouldAbort))
            throw std::runtime_error("Cancelled");
        throw std::runtime_error("HTTPS request failed");
    }

    if (isAbortRequested(shouldAbort))
        throw std::runtime_error("Cancelled");

    long statusCode = curlpp::infos::ResponseCode::get(*request);
    // Handle is healthy again: keep it (and its connection) for the next request.
    releaseEasy(std::move(request));
    if (statusCode < 200 || statusCode >= 300) {
        std::string detail(response.begin(),
                           response.begin() + std::min<std::size_t>(response.size(), 240));
        std::replace(detail.begin(), detail.end(), '\n', ' ');
        std::replace(detail.begin(), detail.end(), '\r', ' ');
        throw std::runtime_error("HTTP error " + std::to_string(statusCode)
                                 + (detail.empty() ? std::string() : ": " + detail));
    }

    return response;
}

bool ensureRuntimeReady() {
    {
        std::shared_lock<std::shared_mutex> lk(g_themeHttpMutex);
        if (runtimeInitializedLocked())
            return true;
    }
    std::unique_lock<std::shared_mutex> lk(g_themeHttpMutex);
    return initializeRuntimeLocked();
}

void resetRuntime() {
    std::unique_lock<std::shared_mutex> lk(g_themeHttpMutex);
    shutdownRuntimeLocked();
}

std::vector<std::uint8_t> performBytes(const std::string& url,
                                       const std::list<std::string>& headers,
                                       const themeshop::http::ProgressCallback& onProgress = {},
                                       const themeshop::http::AbortCheck& shouldAbort = {}) {
    std::string lastError = "Theme Shop HTTP request failed";
    for (int attempt = 1; attempt <= kRequestAttemptCount; ++attempt) {
        if (isAbortRequested(shouldAbort))
            throw std::runtime_error("Cancelled");
        bool retryable = true;
        try {
            if (!ensureRuntimeReady())
                throw std::runtime_error("Theme Shop HTTP runtime is unavailable");

            // Shared lock: any number of transfers run in parallel.
            std::shared_lock<std::shared_mutex> lk(g_themeHttpMutex);
            if (!runtimeInitializedLocked())
                throw std::runtime_error("Theme Shop HTTP runtime is unavailable");

            ensureInternetConnectionReady(url);
            auto bytes = performRequestBytes(url, headers, onProgress, shouldAbort);
            if (attempt > 1) {
                DebugLog::log("[themeshop] request recovered on retry %d: %s", attempt, url.c_str());
            }
            return bytes;
        } catch (const std::exception& ex) {
            lastError = ex.what() ? ex.what() : "";
            if (lastError.empty())
                lastError = "HTTPS request failed";
            if (lastError == "Cancelled" || isAbortRequested(shouldAbort))
                throw std::runtime_error("Cancelled");
            // A definite 4xx answer will not change on retry.
            if (lastError.rfind("HTTP error 4", 0) == 0)
                retryable = false;
            DebugLog::log("[themeshop] request failed (%d/%d): %s -> %s",
                          attempt,
                          kRequestAttemptCount,
                          url.c_str(),
                          lastError.c_str());
        } catch (...) {
            if (isAbortRequested(shouldAbort))
                throw std::runtime_error("Cancelled");
            lastError = "Unknown HTTP error";
            DebugLog::log("[themeshop] request failed (%d/%d): %s -> unknown error",
                          attempt,
                          kRequestAttemptCount,
                          url.c_str());
        }

        if (!retryable)
            break;
        if (attempt < kRequestAttemptCount) {
            // Only tear the runtime down for connection-level failures, not for
            // HTTP status errors (that would also kill other parallel transfers).
            if (lastError.rfind("HTTP error", 0) != 0)
                resetRuntime();
        }
    }

    throw std::runtime_error(lastError);
}

std::string extractHttpDateHeader(const std::string& headerBlock) {
    // curl header callback delivers one header line at a time or a buffer; we
    // accumulate full blocks and scan case-insensitively for "Date:".
    std::string lower = headerBlock;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    constexpr const char* kKey = "date:";
    const auto pos = lower.find(kKey);
    if (pos == std::string::npos)
        return {};
    std::size_t start = pos + 5;
    while (start < headerBlock.size()
           && (headerBlock[start] == ' ' || headerBlock[start] == '\t'))
        ++start;
    std::size_t end = start;
    while (end < headerBlock.size()
           && headerBlock[end] != '\r' && headerBlock[end] != '\n')
        ++end;
    while (end > start && (headerBlock[end - 1] == ' ' || headerBlock[end - 1] == '\t'))
        --end;
    return headerBlock.substr(start, end - start);
}

std::uint64_t performUtcUnixTimeRequest(const std::string& url,
                                        const themeshop::http::AbortCheck& shouldAbort) {
    if (isAbortRequested(shouldAbort))
        throw std::runtime_error("Cancelled");

    std::string headerAccum;
    std::ostringstream body;
    curlpp::Easy request;
    request.setOpt<curlpp::options::Url>(url);
    request.setOpt<curlpp::options::NoBody>(true); // HEAD
    request.setOpt<curlpp::options::FollowLocation>(true);
    request.setOpt<curlpp::options::NoSignal>(true);
    request.setOpt<curlpp::options::ConnectTimeout>(12L);
    request.setOpt<curlpp::options::Timeout>(20L);
    request.setOpt<curlpp::options::IpResolve>((long)CURL_IPRESOLVE_V4);
    request.setOpt<curlpp::options::UserAgent>(std::string("SwitchU/") + SWITCHU_VERSION);
    // Clock may be years off; cert validity checks then fail. This endpoint is
    // only used to read the Date header for bootstrapping the console clock.
    request.setOpt<curlpp::options::SslVerifyPeer>(false);
    request.setOpt<curlpp::options::SslVerifyHost>(0L);
    request.setOpt<curlpp::options::WriteStream>(&body);
    request.setOpt<curlpp::options::HeaderFunction>(
        [&headerAccum, &shouldAbort](char* ptr, size_t size, size_t nmemb) -> size_t {
            if (isAbortRequested(shouldAbort))
                return 0; // abort
            const size_t total = size * nmemb;
            headerAccum.append(ptr, total);
            return total;
        });

    try {
        request.perform();
    } catch (...) {
        if (isAbortRequested(shouldAbort))
            throw std::runtime_error("Cancelled");
        throw;
    }
    if (isAbortRequested(shouldAbort))
        throw std::runtime_error("Cancelled");

    const std::string dateValue = extractHttpDateHeader(headerAccum);
    if (dateValue.empty())
        throw std::runtime_error("No Date header from " + url);

    const time_t parsed = curl_getdate(dateValue.c_str(), nullptr);
    if (parsed < 0)
        throw std::runtime_error("Could not parse Date header: " + dateValue);

    // Sanity: reject absurd results (before 2020 / after ~2100).
    constexpr time_t kMin = 1577836800;  // 2020-01-01 UTC
    constexpr time_t kMax = 4102444800;  // 2100-01-01 UTC
    if (parsed < kMin || parsed > kMax)
        throw std::runtime_error("Date header out of range: " + dateValue);

    DebugLog::log("[themeshop] clock sync Date '%s' -> %lld from %s",
                  dateValue.c_str(),
                  static_cast<long long>(parsed),
                  url.c_str());
    return static_cast<std::uint64_t>(parsed);
}

std::uint64_t performUtcUnixTime(const themeshop::http::AbortCheck& shouldAbort) {
    static const char* kUrls[] = {
        "https://www.cloudflare.com/",
        "https://www.google.com/",
        "https://1.1.1.1/",
    };

    std::string lastError = "Clock sync request failed";

    for (const char* url : kUrls) {
        for (int attempt = 1; attempt <= kRequestAttemptCount; ++attempt) {
            if (isAbortRequested(shouldAbort))
                throw std::runtime_error("Cancelled");
            try {
                if (!ensureRuntimeReady())
                    throw std::runtime_error("Theme Shop HTTP runtime is unavailable");
                std::shared_lock<std::shared_mutex> lk(g_themeHttpMutex);
                if (!runtimeInitializedLocked())
                    throw std::runtime_error("Theme Shop HTTP runtime is unavailable");
                ensureInternetConnectionReady(url);
                return performUtcUnixTimeRequest(url, shouldAbort);
            } catch (const std::exception& ex) {
                lastError = ex.what();
                if (lastError == "Cancelled" || isAbortRequested(shouldAbort))
                    throw std::runtime_error("Cancelled");
                DebugLog::log("[themeshop] clock sync failed (%d/%d): %s -> %s",
                              attempt, kRequestAttemptCount, url, ex.what());
            } catch (...) {
                if (isAbortRequested(shouldAbort))
                    throw std::runtime_error("Cancelled");
                lastError = "Unknown clock sync error";
                DebugLog::log("[themeshop] clock sync failed (%d/%d): %s -> unknown error",
                              attempt, kRequestAttemptCount, url);
            }
            if (attempt < kRequestAttemptCount)
                resetRuntime();
        }
    }

    throw std::runtime_error(lastError);
}

} // namespace

namespace themeshop::http {

bool initialize() {
    return ensureRuntimeReady();
}

void shutdown() {
    resetRuntime();
}

bool isInitialized() {
    std::shared_lock<std::shared_mutex> lk(g_themeHttpMutex);
    return runtimeInitializedLocked();
}

bool isInternetReadyForHttp() {
    if (!ensureRuntimeReady())
        return false;
    std::shared_lock<std::shared_mutex> lk(g_themeHttpMutex);
    if (!runtimeInitializedLocked())
        return false;

    NifmInternetConnectionStatus status = NifmInternetConnectionStatus_ConnectingUnknown1;
    u32 strength = 0;
    const Result rc = nifmGetInternetConnectionStatus(nullptr, &strength, &status);
    if (R_FAILED(rc)) {
        DebugLog::log("[themeshop] isInternetReady nifm status rc=%s",
                      resultToString(rc).c_str());
        return false;
    }
    return status == NifmInternetConnectionStatus_Connected;
}

std::vector<std::uint8_t> getBytes(const std::string& url,
                                   const std::list<std::string>& headers,
                                   const ProgressCallback& onProgress,
                                   const AbortCheck& shouldAbort) {
    return performBytes(url, headers, onProgress, shouldAbort);
}

std::string getText(const std::string& url,
                    const std::list<std::string>& headers,
                    const AbortCheck& shouldAbort) {
    auto bytes = performBytes(url, headers, {}, shouldAbort);
    return std::string(bytes.begin(), bytes.end());
}

std::uint64_t fetchUtcUnixTime(const AbortCheck& shouldAbort) {
    return performUtcUnixTime(shouldAbort);
}

} // namespace themeshop::http
