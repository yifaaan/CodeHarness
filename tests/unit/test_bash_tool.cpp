#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "codeharness/tools/bash_tool.h"

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

}  // namespace

TEST_CASE("bash tool runs command and captures stdout") {
    auto temp = TempDirectory{"codeharness-bash-tool-run-test"};

    auto tool = tools::BashTool{};
    const auto result = tool.execute(
        nlohmann::json{
            {"command", "echo hello"},
            {"timeout_seconds", 5},
        },
        tools::ToolExecutionContext{.cwd = temp.path()});

    REQUIRE(result.ok());
    CHECK(*result == "hello");
    CHECK_FALSE(tool.is_read_only(nlohmann::json::object()));
}

TEST_CASE("bash tool uses cwd override") {
    auto temp = TempDirectory{"codeharness-bash-tool-cwd-test"};
    std::filesystem::create_directories(temp.path() / "subdir");
    {
        std::ofstream file{temp.path() / "subdir" / "marker.txt"};
        file << "marker\n";
    }

    auto tool = tools::BashTool{};
    const auto result = tool.execute(
        nlohmann::json{
#if defined(_WIN32)
            {"command", "dir /b"},
#else
            {"command", "ls"},
#endif
            {"cwd", "subdir"},
            {"timeout_seconds", 5},
        },
        tools::ToolExecutionContext{.cwd = temp.path()});

    REQUIRE(result.ok());
    CHECK(result->find("marker.txt") != std::string::npos);
}

TEST_CASE("bash tool reports command failures") {
    auto temp = TempDirectory{"codeharness-bash-tool-failure-test"};

    auto tool = tools::BashTool{};
    const auto result = tool.execute(
        nlohmann::json{
#if defined(_WIN32)
            {"command", "exit /b 7"},
#else
            {"command", "exit 7"},
#endif
            {"timeout_seconds", 5},
        },
        tools::ToolExecutionContext{.cwd = temp.path()});

    REQUIRE_FALSE(result.ok());
    CHECK(result.status().message().find("(returncode: 7)") != std::string::npos);
}

TEST_CASE("bash tool validates input") {
    auto temp = TempDirectory{"codeharness-bash-tool-validation-test"};

    auto tool = tools::BashTool{};
    const auto empty = tool.execute(nlohmann::json{{"command", ""}},
                                    tools::ToolExecutionContext{.cwd = temp.path()});
    REQUIRE_FALSE(empty.ok());
    CHECK(empty.status().message() == "command must not be empty");

    const auto bad_timeout = tool.execute(
        nlohmann::json{
            {"command", "echo hello"},
            {"timeout_seconds", 0},
        },
        tools::ToolExecutionContext{.cwd = temp.path()});
    REQUIRE_FALSE(bad_timeout.ok());
    CHECK(bad_timeout.status().message() == "timeout_seconds must be between 1 and 600");
}
