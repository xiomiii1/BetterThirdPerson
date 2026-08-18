add_rules("mode.debug", "mode.release")

set_policy("package.requires_lock", true)

package("preloader")
    set_homepage("https://github.com/LiteLDev/preloader-android")
    set_description("Preloader Android")

    add_urls(
        "https://github.com/LiteLDev/preloader-android.git"
    )

    add_versions(
        "main",
        "main"
    )

    add_deps("cmake")

    on_install(
        "android",
        function(package)
            import("package.tools.cmake")
            cmake.install(package)
        end
    )
package_end()

add_requires("preloader")
add_requires("entt")

target("BetterThirdPerson")

    set_kind("shared")

    set_languages("c++20")

    set_strip("all")

    add_files(
        "src/*.cpp"
    )

    add_includedirs(
        "include",
        {
            public = true
        }
    )

    add_packages(
        "preloader",
        "entt"
    )

    if is_plat("android") then

        add_cxflags(
            "-fPIC",
            "-Oz",
            "-ffunction-sections",
            "-fdata-sections",
            "-flto",
            "-fno-unwind-tables",
            "-fno-asynchronous-unwind-tables",
            "-fmerge-all-constants",
            "-fno-stack-protector",
            "-fexceptions",
            "-w",
            "-fvisibility=hidden"
        )

        add_cxxflags(
            "-fno-rtti",
            "-fvisibility-inlines-hidden"
        )

        add_shflags(
            "-Wl,--gc-sections",
            "-Wl,--icf=all",
            "-flto",
            "-Wl,--hash-style=gnu",
            "-Wl,-z,max-page-size=16384"
        )

        add_links(
            "android",
            "log",
            "EGL",
            "GLESv3",
            "GLESv2"
        )

    end

    after_build(
        function(target)

            if not target:is_plat("android") then
                return
            end

            import("lib.detect.find_tool")

            local python =
                find_tool("python3")

            if not python then
                python =
                    find_tool("python")
            end

            assert(
                python,
                "Python 3 is required to package BetterThirdPerson.levipack"
            )

            local package_script =
                path.join(
                    os.projectdir(),
                    "scripts",
                    "package_levipack.py"
                )

            local icon =
                path.join(
                    os.projectdir(),
                    "assets",
                    "betterthirdperson.png"
                )

            local version_header =
                path.join(
                    os.projectdir(),
                    "include",
                    "betterthirdperson",
                    "Version.hpp"
                )

            local output =
                path.join(
                    target:targetdir(),
                    "BetterThirdPerson.levipack"
                )

            os.vrunv(
                python.program,
                {
                    package_script,

                    "--library",
                    target:targetfile(),

                    "--icon",
                    icon,

                    "--version-header",
                    version_header,

                    "--output",
                    output
                }
            )

        end
    )

target_end()
