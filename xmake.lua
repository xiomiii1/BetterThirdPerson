add_rules("mode.debug", "mode.release")
set_policy("package.requires_lock", true)

package("preloader")
    set_homepage("https://github.com/LiteLDev/preloader-android")
    set_description("Preloader Android")
    add_urls("https://github.com/LiteLDev/preloader-android.git")
    add_versions("main", "main")
    add_deps("cmake")
    on_install("android", function (package)
        import("package.tools.cmake").install(package)
    end)
package_end()

add_requires("preloader")
add_requires("entt")

local target = "BetterThirdPerson"
target(target)
    set_kind("shared")
    set_languages("c++20")
    set_strip("all")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_packages("preloader", "entt")
    if is_plat("android") then
        -- Common safe compiler flags for Android
        add_cxflags("-fPIC", "-Oz", "-ffunction-sections", "-fdata-sections", "-flto", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-fmerge-all-constants", "-fno-stack-protector", "-fno-exceptions")
        add_cxxflags("-fno-rtti", "-fvisibility-inlines-hidden", "-fno-exceptions")
        add_shflags("-Wl,--gc-sections", "-Wl,--icf=all", "-flto", "-Wl,--hash-style=gnu", "-Wl,-z,max-page-size=16384")
        add_links("android", "log", "EGL", "GLESv3", "GLESv2")
    end
    after_build(function (target)
        if not target:is_plat("android") then return end
        import("lib.detect.find_tool")
        local python = find_tool("python3") or find_tool("python")
        assert(python, "Python 3 is required")
        -- Call packaging script with required arguments only to avoid syntax/truncation issues
        local args = {path.join(os.projectdir(), "scripts", "package_levipack.py"), "--library", target:targetfile(), "--icon", path.join(os.projectdir(), "assets", "betterthirdperson.png")}
        os.vrunv(python.program, args)
    end)
