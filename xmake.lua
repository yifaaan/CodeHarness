set_project("CodeHarness")
set_version("0.1.0")
set_languages("c++23")

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

if is_mode("debug") then
    set_symbols("debug")
    set_optimize("none")
end

-- ============================================================
-- External libraries (mirroring CodeWhale's Cargo dependencies)
-- ============================================================

-- JSON serialization  (serde_json)
add_requires("nlohmann_json")

-- TOML config parsing (toml)
add_requires("toml11")

-- CLI argument parsing (clap)
add_requires("cli11")

-- Asio networking (async runtime + TCP/UDP/SSL + HTTP via Beast)
add_requires("asio")

-- SQLite persistence (rusqlite)
add_requires("sqlite3")

-- UUID generation (uuid)
add_requires("stduuid")

-- SHA-256 / crypto (sha2)
add_requires("openssl")

-- Date/time (chrono)
add_requires("date")

-- URL parsing
add_requires("ada")

-- Subprocess management
add_requires("reproc")

-- File globbing
add_requires("glob")

-- Unit testing (doctest)
add_requires("doctest")

-- Logging (tracing + tracing-subscriber)
add_requires("spdlog")
add_requires("fmt")

-- ============================================================

local codeharness_sources =
{
    "src/codeharness/cli/cli.cpp",
    "src/codeharness/core/core.cpp",
    "src/codeharness/agent/agent.cpp",
    "src/codeharness/config/config.cpp",
    "src/codeharness/tools/tools.cpp",
    "src/codeharness/execpolicy/execpolicy.cpp",
    "src/codeharness/hooks/hooks.cpp",
    "src/codeharness/mcp/mcp.cpp",
    "src/codeharness/secrets/secrets.cpp",
    "src/codeharness/state/state.cpp",
    "src/codeharness/tui-core/tui_core.cpp",
    "src/codeharness/tui/tui.cpp",
    "src/codeharness/app-server/app_server.cpp",
}

target("codeharness")
    set_kind("binary")
    add_includedirs("src")
    add_files("src/main.cpp", table.unpack(codeharness_sources))
    add_packages(
        "nlohmann_json", "toml11", "cli11", "asio",
        "sqlite3", "stduuid", "openssl",
        "date", "ada", "reproc", "glob",
        "spdlog", "fmt"
    )
    if is_plat("windows") then
        add_syslinks("ws2_32", "crypt32", "bcrypt")
    end

target("codeharness_tests")
    set_kind("binary")
    add_includedirs("src")
    add_files("tests/*.cpp", table.unpack(codeharness_sources))
    add_packages(
        "nlohmann_json", "toml11", "cli11", "asio",
        "sqlite3", "stduuid", "openssl",
        "date", "ada", "reproc", "glob",
        "spdlog", "fmt",
        "doctest"
    )
    if is_plat("windows") then
        add_syslinks("ws2_32", "crypt32", "bcrypt")
    end
