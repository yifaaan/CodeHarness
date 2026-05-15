#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "codeharness/tools/edit_file_tool.h"

using namespace codeharness;

namespace {

    class TempDirectory {
    public:
        explicit TempDirectory(std::string name)
            : path_{std::filesystem::temp_directory_path() / std::move(name)} {
            std::filesystem::remove_all(path_);
            std::filesystem::create_directories(path_);
        }

        ~TempDirectory() { std::filesystem::remove_all(path_); }

        TempDirectory(const TempDirectory&) = delete;
        auto operator=(const TempDirectory&) -> TempDirectory& = delete;

        TempDirectory(TempDirectory&&) = delete;
        auto operator=(TempDirectory&&) -> TempDirectory& = delete;

        [[nodiscard]] auto path() const -> const std::filesystem::path& { return path_; }

    private:
        std::filesystem::path path_;
    };

    [[nodiscard]] auto read_file_text(const std::filesystem::path& path) -> std::string {
        std::ifstream file{path, std::ios::in | std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }

}  // namespace

TEST_CASE("edit file tool replaces the first matching string by default") {
    auto temp = TempDirectory{"codeharness-edit-file-tool-test"};
    const auto file_path = temp.path() / "notes.txt";
    {
        std::ofstream file{file_path, std::ios::out | std::ios::binary};
        file << "alpha\nbeta\nbeta\n";
    }

    auto tool = tools::EditFileTool{};
    const auto result = tool.execute(
        nlohmann::json{
            {"path", "notes.txt"},
            {"old_str", "beta"},
            {"new_str", "gamma"},
        },
        tools::ToolExecutionContext{.cwd = temp.path()});

    REQUIRE(result.ok());
    CHECK(*result == "Updated " + file_path.string());
    CHECK(read_file_text(file_path) == "alpha\ngamma\nbeta\n");
    CHECK_FALSE(tool.is_read_only(nlohmann::json::object()));
}

TEST_CASE("edit file tool can replace every matching string") {
    auto temp = TempDirectory{"codeharness-edit-file-tool-replace-all-test"};
    const auto file_path = temp.path() / "notes.txt";
    {
        std::ofstream file{file_path, std::ios::out | std::ios::binary};
        file << "red blue red\n";
    }

    auto tool = tools::EditFileTool{};
    const auto result = tool.execute(
        nlohmann::json{
            {"path", "notes.txt"},
            {"old_str", "red"},
            {"new_str", "green"},
            {"replace_all", true},
        },
        tools::ToolExecutionContext{.cwd = temp.path()});

    REQUIRE(result.ok());
    CHECK(read_file_text(file_path) == "green blue green\n");
}

TEST_CASE("edit file tool reports missing old string") {
    auto temp = TempDirectory{"codeharness-edit-file-tool-missing-test"};
    const auto file_path = temp.path() / "notes.txt";
    {
        std::ofstream file{file_path, std::ios::out | std::ios::binary};
        file << "unchanged\n";
    }

    auto tool = tools::EditFileTool{};
    const auto result = tool.execute(
        nlohmann::json{
            {"path", "notes.txt"},
            {"old_str", "missing"},
            {"new_str", "value"},
        },
        tools::ToolExecutionContext{.cwd = temp.path()});

    REQUIRE_FALSE(result.ok());
    CHECK(result.status().message() == "old_str was not found in the file");
    CHECK(read_file_text(file_path) == "unchanged\n");
}
