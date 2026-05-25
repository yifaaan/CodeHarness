#include <doctest/doctest.h>

#include <filesystem>

#include "codeharness/tools/sleep_tool.h"

using namespace codeharness;

TEST_CASE("sleep tool accepts zero second sleep") {
    auto tool = tools::SleepTool{};
    const auto result = tool.execute(
        nlohmann::json{{"seconds", 0.0}},
        tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});

    REQUIRE(result.ok());
    CHECK(*result == "Slept for 0 seconds");
    CHECK(tool.is_read_only(nlohmann::json::object()));
}

TEST_CASE("sleep tool validates seconds range") {
    auto tool = tools::SleepTool{};
    const auto too_small = tool.execute(
        nlohmann::json{{"seconds", -0.1}},
        tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});
    REQUIRE_FALSE(too_small.ok());
    CHECK(too_small.status().message() == "seconds must be between 0 and 30");

    const auto too_large = tool.execute(
        nlohmann::json{{"seconds", 30.1}},
        tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});
    REQUIRE_FALSE(too_large.ok());
    CHECK(too_large.status().message() == "seconds must be between 0 and 30");
}
