set_project("CodeHarness")
set_version("0.1.0")

set_languages("c++20")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

add_defines("CODEHARNESS_SOURCE_DIR=\"" .. os.projectdir() .. "\"")

-- 当前 runtime 直接依赖的第三方包。
local runtime_packages = {
    "cli11",
    "expected-lite",
    "fmt",
    "glob",
    "nlohmann_json",
    "re2",
    "reproc",
    "spdlog",
    "yaml-cpp",
}

-- Phase 4/5（MCP、Plugin、ohmo、Gateway）预留依赖。
-- 启用时移到 runtime_packages，并在对应 target 中 add_packages 即可。
--[[
local phase_packages = {
    "ada",       -- URL parser (Phase 4: HTTP transport)
    "asio",      -- async I/O (Phase 4: MCP + Phase 5: Mailbox)
    "brotli",    -- HTTP compression
    "openssl",   -- TLS
    "sqlite3",   -- session / memory storage
    "zlib",      -- HTTP compression
}
--]]

for _, package in ipairs(runtime_packages) do
    add_requires(package)
end

add_requires("doctest", {optional = true})

target("codeharness")
    set_kind("binary")
    add_files("src/**.cpp")
    add_includedirs("src")
    for _, package in ipairs(runtime_packages) do
        add_packages(package)
    end

target("codeharness_tests")
    set_kind("binary")
    add_files("tests/**.cpp")
    add_files("src/codeharness/**.cpp")
    add_includedirs("src")
    add_packages("doctest")
    for _, package in ipairs(runtime_packages) do
        add_packages(package)
    end
