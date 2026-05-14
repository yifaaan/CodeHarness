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

local codeharness_core_sources = {
    "src/codeharness/app/*.cpp",
    "src/codeharness/logging.cpp",
    "src/codeharness/api/*.cpp",
    "src/codeharness/config/*.cpp",
    "src/codeharness/engine/*.cpp",
    "src/codeharness/permissions/*.cpp",
    "src/codeharness/services/*.cpp",
    "src/codeharness/tools/*.cpp",
}

target("codeharness")
    set_kind("binary")
    add_includedirs("src")
    add_defines("SPDLOG_FMT_EXTERNAL")
    add_files("src/cli/main.cpp", table.unpack(codeharness_core_sources))
    add_packages("abseil", "cli11", "nlohmann_json", "fmt", "spdlog", "libcurl")

target("codeharness_tests")
    set_kind("binary")
    add_includedirs("src", "tests/support")
    add_defines("SPDLOG_FMT_EXTERNAL")
    add_files("tests/unit/*.cpp", table.unpack(codeharness_core_sources))
    add_packages("abseil", "doctest", "nlohmann_json", "fmt", "spdlog", "libcurl")
