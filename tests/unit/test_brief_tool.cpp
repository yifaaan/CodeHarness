#include <doctest/doctest.h>

#include <filesystem>

#include "codeharness/tools/brief_tool.h"

using namespace codeharness;

TEST_CASE("brief tool trims text without shortening when it fits") {
    auto tool = tools::BriefTool{};
    const auto result =
        tool.execute(nlohmann::json{{"text", "  short text  "}},
                     tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});

    REQUIRE(result.ok());
    CHECK(*result == "short text");
    CHECK(tool.is_read_only(nlohmann::json::object()));
}

TEST_CASE("brief tool shortens text to max chars and appends ellipsis") {
    auto tool = tools::BriefTool{};
    const auto result = tool.execute(
        nlohmann::json{
            {"text", "abcdefghijklmnopqrstuvwxyz"},
            {"max_chars", 20},
        },
        tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});

    REQUIRE(result.ok());
    CHECK(*result == "abcdefghijklmnopqrst...");
}

TEST_CASE("brief tool trims trailing whitespace before ellipsis") {
    auto tool = tools::BriefTool{};
    const auto result = tool.execute(
        nlohmann::json{
            {"text", "alpha beta gamma    delta"},
            {"max_chars", 20},
        },
        tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});

    REQUIRE(result.ok());
    CHECK(*result == "alpha beta gamma...");
}

TEST_CASE("brief tool validates max chars") {
    auto tool = tools::BriefTool{};
    const auto result = tool.execute(
        nlohmann::json{
            {"text", "abcdefghijklmnopqrstuvwxyz"},
            {"max_chars", 19},
        },
        tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});

    REQUIRE_FALSE(result.ok());
    CHECK(result.status().message() == "max_chars must be between 20 and 2000");
}
