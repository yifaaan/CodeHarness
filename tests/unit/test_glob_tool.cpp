// tests/unit/test_glob_tool.cpp
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "codeharness/tools/glob_tool.h"

using namespace codeharness;

namespace {
    void write_text(const std::filesystem::path& path) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file{path};
        file << "demo\n";
    }
}  // namespace

TEST_CASE("glob tool lists matching files") {
    const auto cwd = std::filesystem::temp_directory_path() / "codeharness-glob-tool-test";
    std::filesystem::remove_all(cwd);

    write_text(cwd / "a.py");
    write_text(cwd / "b.py");
    write_text(cwd / "src" / "c.py");

    auto tool = tools::GlobTool{};
    const auto result =
        tool.execute(nlohmann::json{{"pattern", "*.py"}}, tools::ToolExecutionContext{.cwd = cwd});

    REQUIRE(result.ok());
    CHECK(*result == "a.py\nb.py");
    CHECK(tool.is_read_only(nlohmann::json::object()));

    std::filesystem::remove_all(cwd);
}

TEST_CASE("glob tool supports recursive star star") {
    const auto cwd = std::filesystem::temp_directory_path() / "codeharness-glob-recursive-test";
    std::filesystem::remove_all(cwd);

    write_text(cwd / "src" / "main.cpp");
    write_text(cwd / "tests" / "test_main.cpp");

    auto tool = tools::GlobTool{};
    const auto result = tool.execute(nlohmann::json{{"pattern", "**/*.cpp"}},
                                     tools::ToolExecutionContext{.cwd = cwd});

    REQUIRE(result.ok());
    CHECK(*result == "src/main.cpp\ntests/test_main.cpp");

    std::filesystem::remove_all(cwd);
}