set_project("CodeHarness")
set_version("0.1.0")
set_languages("c++20")

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

add_requires("abseil", "cli11", "nlohmann_json", "fmt", "spdlog", "doctest", "libcurl")

if is_mode("debug") then
    set_symbols("debug")
    set_optimize("none")
end

if is_plat("windows") then
    add_cxxflags("/W4", {tools = {"cl"}})
else
    add_cxxflags("-Wall", "-Wextra", "-Wpedantic", {tools = {"gcc", "clang"}})
end

target("codeharness")
    set_kind("binary")
    add_includedirs("src")
    add_defines("SPDLOG_FMT_EXTERNAL")
    add_files("src/cli/main.cpp")
    add_files("src/codeharness/app/*.cpp")
    add_files("src/codeharness/logging.cpp")
    add_files("src/codeharness/api/*.cpp")
    add_files("src/codeharness/config/*.cpp")
    add_files("src/codeharness/engine/*.cpp")
    add_files("src/codeharness/permissions/*.cpp")
    add_files("src/codeharness/services/*.cpp")
    add_files("src/codeharness/tools/*.cpp")
    add_files("src/codeharness/ui/*.cpp")
    add_packages("abseil", "cli11", "nlohmann_json", "fmt", "spdlog", "libcurl")

target("codeharness_tests")
    set_kind("binary")
    add_includedirs("src", "tests/support")
    add_defines("SPDLOG_FMT_EXTERNAL")
    add_files("tests/unit/*.cpp")
    add_files("src/codeharness/app/*.cpp")
    add_files("src/codeharness/logging.cpp")
    add_files("src/codeharness/api/*.cpp")
    add_files("src/codeharness/config/*.cpp")
    add_files("src/codeharness/engine/*.cpp")
    add_files("src/codeharness/permissions/*.cpp")
    add_files("src/codeharness/services/*.cpp")
    add_files("src/codeharness/tools/*.cpp")
    add_files("src/codeharness/ui/*.cpp")
    add_packages("abseil", "doctest", "nlohmann_json", "fmt", "spdlog", "libcurl")
