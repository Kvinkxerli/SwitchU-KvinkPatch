set_project("SwitchU")

add_repositories("switchu-repo .")
add_repositories("switch-repo https://github.com/PoloNX/switch-repo.git")

includes("toolchain/*.lua")
add_rules("mode.debug", "mode.release")

local version = "1.3.0"
local version_define = string.format('SWITCHU_VERSION="%s"', version)

set_version(version)

add_requires("libsdl", "libsdl_mixer", "libsdl_ttf", "zlib", "libwebp", "nlohmann_json", "fmt", "libcurl", "curlpp", {configs = {toolchains = "devkita64"}})
if get_config("backend") ~= "sdl2" then
    add_requires("deko3d", {configs = {toolchains = "devkita64"}})
    if is_mode("debug") then
        add_requires("imgui", {configs = {toolchains = "devkita64"}})
    end
end

option("homebrew")
    set_default(false)
    set_showmenu(true)
    set_description("Build a standalone .nro homebrew for testing")
option_end()

option("backend")
    set_default("deko3d")
    set_showmenu(true)
    set_description("GPU rendering backend: deko3d or sdl2")
    set_values("deko3d", "sdl2")
option_end()

target("nxui")
    set_kind("static")
    set_default(false)
    if not is_plat("cross") then return end

    set_toolchains("devkita64")
    set_languages("c++20")

    add_files("lib/nxui/src/core/Application.cpp")
    add_files("lib/nxui/src/core/Animation.cpp")
    add_files("lib/nxui/src/core/I18n.cpp")
    add_files("lib/nxui/src/core/Input.cpp")
    add_files("lib/nxui/src/core/Theme.cpp")
    add_files("lib/nxui/src/widgets/*.cpp")
    add_files("lib/nxui/src/focus/*.cpp")
    add_files("lib/nxui/src/core/Font.cpp")

    if get_config("backend") == "sdl2" then
        add_defines("NXUI_BACKEND_SDL2", {public = true})
        add_files("lib/nxui/src/core/GpuDevice_sdl2.cpp")
        add_files("lib/nxui/src/core/Renderer_sdl2.cpp")
        add_files("lib/nxui/src/core/Texture_sdl2.cpp")
    else
        add_defines("NXUI_BACKEND_DEKO3D", {public = true})
        add_files("lib/nxui/src/core/GpuDevice.cpp")
        add_files("lib/nxui/src/core/Renderer.cpp")
        add_files("lib/nxui/src/core/Texture.cpp")
    end

    add_includedirs("lib/nxui/include", {public = true})
    add_includedirs("lib/nxui/include/nxui/third_party/stb")

    add_packages("libsdl", "libsdl_ttf", "libwebp")

    add_cxxflags("-frtti", "-fexceptions", {force = true})
    if get_config("backend") == "deko3d" then
        add_packages("deko3d", {links = is_mode("debug") and "deko3dd" or "deko3d"})
    end

    if is_mode("release") then
        -- LTO disabled: -flto=auto stalled local release links and produced
        -- "Input file doesn't fit ELF header" failures on some toolchains.
        add_cxflags("-O3", "-ffast-math", {force = true})
    end
target_end()

target("espeak-ucd")
    set_kind("static")
    set_default(false)
    if not is_plat("cross") then return end

    set_toolchains("devkita64")
    set_languages("gnu11")

    add_files("lib/espeak-ng/src/ucd-tools/src/case.c")
    add_files("lib/espeak-ng/src/ucd-tools/src/categories.c")
    add_files("lib/espeak-ng/src/ucd-tools/src/ctype.c")
    add_files("lib/espeak-ng/src/ucd-tools/src/proplist.c")
    add_files("lib/espeak-ng/src/ucd-tools/src/scripts.c")
    add_files("lib/espeak-ng/src/ucd-tools/src/tostring.c")

    add_includedirs("lib/espeak-ng/src/ucd-tools/src/include", {public = true})
target_end()

target("espeak-ng")
    set_kind("static")
    set_default(false)
    if not is_plat("cross") then return end

    set_toolchains("devkita64")
    set_languages("gnu11")

    add_deps("espeak-ucd")

    add_files("lib/espeak-ng/src/libespeak-ng/common.c")
    add_files("lib/espeak-ng/src/libespeak-ng/mnemonics.c")
    add_files("lib/espeak-ng/src/libespeak-ng/error.c")
    add_files("lib/espeak-ng/src/libespeak-ng/ieee80.c")
    add_files("lib/espeak-ng/src/libespeak-ng/compiledata.c")
    add_files("lib/espeak-ng/src/libespeak-ng/compiledict.c")
    add_files("lib/espeak-ng/src/libespeak-ng/dictionary.c")
    add_files("lib/espeak-ng/src/libespeak-ng/encoding.c")
    add_files("lib/espeak-ng/src/libespeak-ng/intonation.c")
    add_files("lib/espeak-ng/src/libespeak-ng/langopts.c")
    add_files("lib/espeak-ng/src/libespeak-ng/numbers.c")
    add_files("lib/espeak-ng/src/libespeak-ng/phoneme.c")
    add_files("lib/espeak-ng/src/libespeak-ng/phonemelist.c")
    add_files("lib/espeak-ng/src/libespeak-ng/readclause.c")
    add_files("lib/espeak-ng/src/libespeak-ng/setlengths.c")
    add_files("lib/espeak-ng/src/libespeak-ng/soundicon.c")
    add_files("lib/espeak-ng/src/libespeak-ng/spect.c")
    add_files("lib/espeak-ng/src/libespeak-ng/ssml.c")
    add_files("lib/espeak-ng/src/libespeak-ng/synthdata.c")
    add_files("lib/espeak-ng/src/libespeak-ng/synthesize.c")
    add_files("lib/espeak-ng/src/libespeak-ng/tr_languages.c")
    add_files("lib/espeak-ng/src/libespeak-ng/translate.c")
    add_files("lib/espeak-ng/src/libespeak-ng/translateword.c")
    add_files("lib/espeak-ng/src/libespeak-ng/voices.c")
    add_files("lib/espeak-ng/src/libespeak-ng/wavegen.c")
    add_files("lib/espeak-ng/src/libespeak-ng/speech.c")
    add_files("lib/espeak-ng/src/libespeak-ng/espeak_api.c")

    add_includedirs("projects/menu/src/core/espeak_config", {public = false})
    add_includedirs("lib/nxui/include", {public = false})
    add_includedirs("lib/espeak-ng/src/include", {public = true})
    add_includedirs("lib/espeak-ng/src/libespeak-ng", {public = false})
    add_includedirs("lib/espeak-ng/src/ucd-tools/src/include", {public = false})
    add_defines("LIBESPEAK_NG_EXPORT=1")
    add_defines("_GNU_SOURCE")
    add_defines('PATH_ESPEAK_DATA="romfs:/espeak-ng-data"')
    add_cflags("-fwrapv", "-fvisibility=hidden", {force = true})
target_end()

target("atmosphere-stratosphere")
    set_kind("phony")
    set_default(false)
    if not is_plat("cross") then return end

    on_build(function (target)
        import("scripts.stamp")
        local source_dir = path.join(os.projectdir(), "lib/Atmosphere-libs")
        local library = path.join(source_dir,
            "libstratosphere/lib/nintendo_nx_arm64_armv8a/release/libstratosphere.a")
        local revision = stamp.revision(source_dir)
        if os.isfile(library) and stamp.matches("libstratosphere", revision) then
            cprint("${color.build.target}cached${clear} libstratosphere")
            return
        end
        -- Build libstratosphere in parallel (the stock "nx_release" wrapper runs
        -- a serial make, ~20 min). Calling libstratosphere.mk directly lets us
        -- pass -j, drop debug info and silence third-party [[nodiscard]] noise.
        local jobs = tostring(math.max(os.cpuinfo("ncpu") or 2, 2))
        local branch = "detached"
        local ok, out = pcall(os.iorunv, "git", {"-C", source_dir, "symbolic-ref", "--short", "HEAD"})
        if ok and out and #out:trim() > 0 then branch = out:trim() end
        os.execv("make", {
            "-C", "lib/Atmosphere-libs/libstratosphere",
            "-f", "libstratosphere.mk",
            "-j" .. jobs,
            "ATMOSPHERE_MAKEFILE_TARGET=nx_release",
            "ATMOSPHERE_BUILD_NAME=release",
            "ATMOSPHERE_BOARD=nx-hac-001",
            "ATMOSPHERE_CPU=arm-cortex-a57",
            "ATMOSPHERE_GIT_BRANCH=" .. branch,
            "ATMOSPHERE_BUILD_SETTINGS=-Wno-unused-result -g0"
        })
        stamp.write("libstratosphere", revision)
    end)
target_end()

target("SwitchU")
    set_kind("binary")
    if not is_plat("cross") then return end

    set_toolchains("devkita64")
    set_languages("c++20")
    add_rules("switch")

    add_deps("nxui")
    add_includedirs("projects/common/include", {public = false})
    add_includedirs("projects/menu/src", {public = false})
    add_files("projects/menu/src/**.cpp")
    add_packages("nlohmann_json", "fmt", "libsdl", "libsdl_mixer", "libsdl_ttf", "zlib", "libwebp", "libcurl", "curlpp")
    add_linkgroups("SDL2_ttf", "harfbuzz-subset", "harfbuzz", "freetype", "png16", "bz2", "z", {group = true})
    add_linkgroups("SDL2_mixer", "FLAC++", "FLAC", "vorbisidec", "ogg", "modplug", "opusurl", "opusfile", "opus", {group = true})

    if is_mode("debug") and get_config("backend") ~= "sdl2" then
        add_packages("imgui")
        add_defines("SWITCHU_DEBUG_UI")
    end

    add_cxxflags("-frtti", "-fexceptions", {force = true})
    if get_config("backend") == "deko3d" then
        add_packages("deko3d", {links = is_mode("debug") and "deko3dd" or "deko3d"})
        add_linkorders(is_mode("debug") and "deko3dd" or "deko3d", "nx")
    end
    add_syslinks("nx")

    if is_mode("release") then
        add_cxflags("-O3", "-ffast-math", {force = true})
    end

    add_defines(version_define)
    add_deps("espeak-ng")
    add_includedirs("lib/espeak-ng/src/include")
    add_syslinks("m")

    before_build(function(target)
        local source_dir = path.join(os.projectdir(), "lib/espeak-ng")
        local build_dir = path.join(os.projectdir(), "build/espeak-ng-native")
        local data_dir = path.join(build_dir, "espeak-ng-data")

        if not os.isdir(path.join(source_dir, "CMakeLists.txt")) then
            raise("eSpeak NG submodule is missing/incomplete: " .. source_dir ..
                  "\nRun: git submodule update --init --recursive lib/espeak-ng")
        end

        import("scripts.stamp")
        local revision = stamp.revision(source_dir)
        if os.isfile(path.join(data_dir, "phondata")) and
           os.isfile(path.join(data_dir, "fr_dict")) and
           stamp.matches("espeak-ng-data", revision) then
            cprint("${color.build.target}cached${clear} eSpeak NG data")
            return
        end

        -- The espeak-ng data target must be built with the *host* toolchain.
        -- Under devkitA64/MSYS2 CC/CXX point at aarch64-none-elf-g++ and CMake
        -- dies with "CMAKE_C_COMPILER not set"; on Windows without nmake it also
        -- needs an explicit generator. Force host compilers + Ninja here.
        cprint("${color.build.target}generating${clear} eSpeak NG data")

        local function find_tool(names)
            for _, name in ipairs(names) do
                local p = os.programr(name)
                if p then return p end
            end
            return nil
        end

        local host_cc = find_tool({"gcc", "cc", "clang"})
        local host_cxx = find_tool({"g++", "c++", "clang++"})
        local ninja = find_tool({"ninja", "ninja.exe"})

        local cfgargs = {
            "-S", source_dir,
            "-B", build_dir,
            "--fresh",
            "-DENABLE_TESTS=OFF",
            "-DBUILD_SHARED_LIBS=OFF",
            "-DUSE_LIBSONIC=OFF",
            "-DUSE_LIBPCAUDIO=OFF",
            "-DUSE_MBROLA=OFF",
        }
        if host_cc  then table.insert(cfgargs, "-DCMAKE_C_COMPILER="   .. host_cc)  end
        if host_cxx then table.insert(cfgargs, "-DCMAKE_CXX_COMPILER=" .. host_cxx) end
        if ninja then
            table.insert(cfgargs, "-G")
            table.insert(cfgargs, "Ninja")
        elseif is_host("windows") then
            -- Last resort: let CMake probe Visual Studio itself; if that also
            -- fails the user needs to install VS Build Tools or run from a
            -- "Developer Command Prompt".
            table.insert(cfgargs, "-G")
            table.insert(cfgargs, "Visual Studio 17 2022")
        end

        -- Do NOT pass DEVKITPRO/DEVKITARM through to CMake: its find_program
        -- probes would pick up cross tools again.
        local saved = {}
        for _, k in ipairs({"CC", "CXX", "DEVKITPRO", "DEVKITARM", "DFU_PREFIX"}) do
            saved[k] = os.getenv(k)
            os.setenv(k, nil)
        end
        local ok, err = os.execv("cmake", cfgargs)
        local build_ok
        if ok then
            build_ok, err = os.execv("cmake", {"--build", build_dir, "--target", "data"})
        end
        for k, v in pairs(saved) do os.setenv(k, v) end

        if not ok then
            raise("cmake configure of eSpeak NG failed: " .. tostring(err) ..
                  "\nInstall a host compiler (e.g. MSYS2: pacman -S mingw-w64-x86_64-gcc ninja)" ..
                  " or run the build from a Visual Studio Developer prompt.")
        end
        if not build_ok then
            raise("cmake build of eSpeak NG data failed: " .. tostring(err))
        end

        if not os.isfile(path.join(data_dir, "phondata")) or
           not os.isfile(path.join(data_dir, "fr_dict")) then
            raise("eSpeak NG data generation did not produce required runtime files")
        end
        stamp.write("espeak-ng-data", revision)
    end)

    if has_config("homebrew") then
        add_defines("SWITCHU_HOMEBREW")
        set_values("switch.name",    "SwitchU")
        set_values("switch.author",  "PoloNX")
        set_values("switch.version", version)
        set_values("switch.romfs",   "romfs")
        set_values("switch.tid",     "0100000000001000")
        set_values("switch.json",    "SwitchU.json")
        set_values("switch.format",  "nro")
    else
        add_deps("switchu-daemon")

        add_defines("SWITCHU_MENU")
        set_values("switch.name",    "switchu-menu")
        set_values("switch.author",  "PoloNX")
        set_values("switch.version", version)
        set_values("switch.romfs",   "romfs")
        set_values("switch.json",    "projects/menu/menu.json")
        set_values("switch.format",  "nsp")
        set_values("switch.install_contents", false)
        set_values("switch.assets_dir", "SwitchU")
        set_values("switch.raw_exefs_dir", "switch/SwitchU/bin/menu")
    end
target_end()

target("SwitchU-Manager")
    set_kind("binary")
    if not is_plat("cross") then return end

    set_toolchains("devkita64")
    set_languages("c++20")
    add_rules("switch")

    add_deps("nxui")
    add_includedirs("projects/common/include", {public = false})
    add_includedirs("projects/menu/src", {public = false})
    add_includedirs("projects/manager/src", {public = false})
    add_files("projects/manager/src/**.cpp")
    add_files("projects/menu/src/widgets/ActionButton.cpp")
    add_files("projects/menu/src/widgets/OverlayDialog.cpp")
    add_files("projects/menu/src/widgets/SelectionCursor.cpp")
    add_packages("libsdl", "libsdl_ttf", "zlib", "libwebp", "libcurl", "nlohmann_json")
    add_links("minizip")
    add_linkgroups("SDL2_ttf", "harfbuzz-subset", "harfbuzz", "freetype", "png16", "bz2", "z", {group = true})

    add_cxxflags("-frtti", "-fexceptions", {force = true})
    if get_config("backend") == "deko3d" then
        add_packages("deko3d", {links = is_mode("debug") and "deko3dd" or "deko3d"})
        add_linkorders(is_mode("debug") and "deko3dd" or "deko3d", "nx")
    end
    add_syslinks("nx")
    add_defines(version_define)

    if is_mode("release") then
        add_cxflags("-O3", "-ffast-math", {force = true})
    end

    before_build(function(target)
        local romfs_dir = path.join(os.projectdir(), "build/manager-romfs")
        local font_dir = path.join(romfs_dir, "fonts")
        local i18n_dir = path.join(romfs_dir, "i18n")
        os.mkdir(font_dir)
        os.mkdir(i18n_dir)
        os.cp(path.join(os.projectdir(), "romfs/fonts/DejaVuSans.ttf"), font_dir)
        os.cp(path.join(os.projectdir(), "projects/manager/romfs/i18n/*.json"), i18n_dir)
    end)

    set_values("switch.name",    "SwitchU-Manager")
    set_values("switch.author",  "PoloNX")
    set_values("switch.version", version)
    set_values("switch.romfs",   "build/manager-romfs")
    set_values("switch.icon",    "projects/manager/icon.jpg")
    set_values("switch.format",  "nro")
target_end()

target("switchu-daemon")
    set_kind("binary")
    if not is_plat("cross") then return end
    set_default(not has_config("homebrew"))

    set_toolchains("devkita64")
    set_languages("c++20")
    add_rules("switch")

    add_deps("atmosphere-stratosphere")

    add_files("projects/daemon/src/**.cpp")

    add_includedirs("projects/common/include", {public = false})
    add_includedirs("lib/Atmosphere-libs/libstratosphere/include")
    add_includedirs("lib/Atmosphere-libs/libvapours/include")

    add_defines(
        "ATMOSPHERE",
        "ATMOSPHERE_IS_STRATOSPHERE",
        "ATMOSPHERE_OS_HORIZON",
        "ATMOSPHERE_BOARD_NINTENDO_NX",
        "ATMOSPHERE_ARCH_ARM64",
        "ATMOSPHERE_ARCH_ARM_V8A",
        "_GNU_SOURCE"
    )
    add_cxxflags("-fno-rtti", "-fexceptions", "-std=gnu++23", {force = true})
    add_packages("zlib")
    add_linkdirs("lib/Atmosphere-libs/libstratosphere/lib/nintendo_nx_arm64_armv8a/release")
    add_links("stratosphere")
    add_syslinks("nx")

    if is_mode("release") then
        add_cxflags("-O3", "-ffast-math", {force = true})
    end

    set_values("switch.name",    "switchu-daemon")
    set_values("switch.author",  "PoloNX")
    set_values("switch.version", version)
    set_values("switch.tid",     "0100000000001000")
    set_values("switch.json",    "projects/daemon/daemon.json")
    set_values("switch.format",  "nsp")
target_end()
