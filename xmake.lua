set_project("CodeHarness")
set_version("0.1.0")

set_languages("c++20")
set_encodings("utf-8")

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

local runtime_packages = {
    "ada",
    "asio",
    "brotli",
    "cli11",
    "expected-lite",
    "fmt",
    "nlohmann_json",
    "openssl",
    "pcre2",
    "reproc",
    "spdlog",
    "sqlite3",
    "stduuid",
    "yaml-cpp",
    "zlib"
}

for _, package in ipairs(runtime_packages) do
    if package == "stduuid" then
        add_requires(package, {configs = {span = true}})
    else
        add_requires(package)
    end
end

add_requires("doctest")

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
