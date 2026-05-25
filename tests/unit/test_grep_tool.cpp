#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "codeharness/tools/grep_tool.h"

using namespace codeharness;

namespace {
    void write_text(const std::filesystem::path& path, const std::string& content) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file{path, std::ios::out | std::ios::binary};
        file << content;
    }
}  // namespace

TEST_CASE("grep tool searches matching text files") {
    const auto cwd = std::filesystem::temp_directory_path() / "codeharness-grep-tool-test";
    std::filesystem::remove_all(cwd);

    write_text(cwd / "a.py", "def alpha():\n    return 1\n");
    write_text(cwd / "b.py", "def beta():\n    return 2\n");
    write_text(cwd / "README.md", "def beta is mentioned here\n");

    auto tool = tools::GrepTool{};
    const auto result =
        tool.execute(nlohmann::json{{"pattern", R"(def\s+beta)"}, {"file_glob", "*.py"}},
                     tools::ToolExecutionContext{.cwd = cwd});

    REQUIRE(result.ok());
    CHECK(*result == "b.py:1:def beta():");
    CHECK(tool.is_read_only(nlohmann::json::object()));

    std::filesystem::remove_all(cwd);
}

TEST_CASE("grep tool supports root and case-insensitive matching") {
    const auto cwd =
        std::filesystem::temp_directory_path() / "codeharness-grep-case-insensitive-test";
    std::filesystem::remove_all(cwd);

    write_text(cwd / "src" / "notes.txt", "Alpha\nbeta\n");
    write_text(cwd / "other" / "notes.txt", "alpha outside root\n");

    auto tool = tools::GrepTool{};
    const auto result = tool.execute(
        nlohmann::json{
            {"pattern", "alpha"},
            {"root", "src"},
            {"file_glob", "*.txt"},
            {"case_sensitive", false},
        },
        tools::ToolExecutionContext{.cwd = cwd});

    REQUIRE(result.ok());
    CHECK(*result == "notes.txt:1:Alpha");

    std::filesystem::remove_all(cwd);
}

TEST_CASE("grep tool skips binary files and honors limit") {
    const auto cwd = std::filesystem::temp_directory_path() / "codeharness-grep-limit-test";
    std::filesystem::remove_all(cwd);

    write_text(cwd / "src" / "a.txt", "needle one\n");
    write_text(cwd / "src" / "b.txt", "needle two\n");
    write_text(cwd / "src" / "binary.txt", std::string{"needle\0hidden\n", 14});

    auto tool = tools::GrepTool{};
    const auto result = tool.execute(
        nlohmann::json{
            {"pattern", "needle"},
            {"file_glob", "**/*.txt"},
            {"limit", 1},
        },
        tools::ToolExecutionContext{.cwd = cwd});

    REQUIRE(result.ok());
    CHECK(*result == "src/a.txt:1:needle one");

    std::filesystem::remove_all(cwd);
}
