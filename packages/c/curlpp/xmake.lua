-- Local override of the xmake-repo "curlpp" package.
--
-- Identical to the official recipe except for the missing on_test(): the
-- official test links an executable against libcurl.a, and the current
-- devkitPro switch-curl is built with the libnx TLS backend, so that link
-- needs -lnx which the test does not pass (undefined reference to
-- sslConnectionClose, csrngGetRandomBytes, ...). The static library itself
-- builds fine; the real executables link libnx through the project targets.
package("curlpp")
    set_homepage("http://www.curlpp.org")
    set_description("C++ wrapper around libcURL")
    set_license("MIT")

    add_urls("https://github.com/jpbarrette/curlpp.git")
    add_versions("2026.02.12", "ec1b66e699557cd9d608d322c013a1ebda16bd08")
    add_versions("2023.07.27", "1d8c7876cc81d7d125b663066282b207d9cbfe9a")

    add_deps("libcurl")

    on_install("windows", "linux", "macosx", "mingw", "cross", function (package)
        io.writefile("xmake.lua", ([[
            set_languages("c++11")
            add_rules("mode.debug", "mode.release")
            add_requires("libcurl")
            target("curlpp")
                set_kind("$(kind)")
                add_files("src/**.cpp")
                add_includedirs("include/")
                add_headerfiles("include/(**.hpp)", "include/(**.inl)")
                add_packages("libcurl")
                if is_plat("windows") and is_kind("shared") then
                    add_rules("utils.symbols.export_all", {export_classes = true})
                end
        ]]))
        import("package.tools.xmake").install(package)
    end)
package_end()
