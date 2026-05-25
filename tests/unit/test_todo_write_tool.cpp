#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "codeharness/tools/todo_write_tool.h"

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

    [[nodiscard]] auto read_text(const std::filesystem::path& path) -> std::string {
        std::ifstream file{path, std::ios::in | std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }

}  // namespace

TEST_CASE("todo write tool creates default TODO file") {
    auto temp = TempDirectory{"codeharness-todo-write-create-test"};
    const auto file_path = temp.path() / "TODO.md";

    auto tool = tools::TodoWriteTool{};
    const auto result =
        tool.execute(nlohmann::json{{"item", "wire next module"}},
                     tools::ToolExecutionContext{.cwd = temp.path()});

    REQUIRE(result.ok());
    CHECK(*result == "Updated " + file_path.string());
    CHECK(read_text(file_path) == "# TODO\n- [ ] wire next module\n");
    CHECK_FALSE(tool.is_read_only(nlohmann::json::object()));
}

TEST_CASE("todo write tool appends checked item to existing file") {
    auto temp = TempDirectory{"codeharness-todo-write-append-test"};
    const auto file_path = temp.path() / "docs" / "todo.md";
    std::filesystem::create_directories(file_path.parent_path());
    {
        std::ofstream file{file_path, std::ios::out | std::ios::binary};
        file << "# TODO\n\n- [ ] existing\n\n";
    }

    auto tool = tools::TodoWriteTool{};
    const auto result = tool.execute(
        nlohmann::json{
            {"item", "done item"},
            {"checked", true},
            {"path", "docs/todo.md"},
        },
        tools::ToolExecutionContext{.cwd = temp.path()});

    REQUIRE(result.ok());
    CHECK(read_text(file_path) == "# TODO\n\n- [ ] existing\n- [x] done item\n");
}

TEST_CASE("todo write tool validates required text") {
    auto temp = TempDirectory{"codeharness-todo-write-validation-test"};

    auto tool = tools::TodoWriteTool{};
    const auto result = tool.execute(nlohmann::json{{"item", ""}},
                                     tools::ToolExecutionContext{.cwd = temp.path()});

    REQUIRE_FALSE(result.ok());
    CHECK(result.status().message() == "item must not be empty");
}
