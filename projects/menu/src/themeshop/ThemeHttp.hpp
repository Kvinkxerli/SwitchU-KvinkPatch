#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <vector>

namespace themeshop::http {

using ProgressCallback = std::function<void(std::uint64_t downloaded,
                                            std::uint64_t total)>;
/// Return true to abort the in-flight transfer as soon as curl polls progress.
using AbortCheck = std::function<bool()>;

bool initialize();
void shutdown();
bool isInitialized();
/// True when NIFM reports an active internet connection (initializes HTTP runtime if needed).
bool isInternetReadyForHttp();

std::vector<std::uint8_t> getBytes(const std::string& url,
                                   const std::list<std::string>& headers = {},
                                   const ProgressCallback& onProgress = {},
                                   const AbortCheck& shouldAbort = {});

std::string getText(const std::string& url,
                    const std::list<std::string>& headers = {},
                    const AbortCheck& shouldAbort = {});

/// Fetch current UTC unix time from public HTTPS Date headers.
/// Uses relaxed TLS verification so a badly wrong console clock can still
/// bootstrap (Nintendo auto-sync is blocked under common DNS filters).
std::uint64_t fetchUtcUnixTime(const AbortCheck& shouldAbort = {});

} // namespace themeshop::http
