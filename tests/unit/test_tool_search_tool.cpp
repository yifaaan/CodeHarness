#include <doctest/doctest.h>

#include <filesystem>
#include <memory>

#include "codeharness/tools/grep_tool.h"
#include "codeharness/tools/read_file_tool.h"
#include "codeharness/tools/tool_registry.h"
#include "codeharness/tools/tool_search_tool.h"
#include "codeharness/tools/write_file_tool.h"

using namespace codeharness;

TEST_CASE("tool search finds tools by name and description") {
    auto registry = tools::ToolRegistry{};
    registry.register_tool(std::make_unique<tools::ReadFileTool>());
    registry.register_tool(std::make_unique<tools::WriteFileTool>());
    registry.register_tool(std::make_unique<tools::GrepTool>());
    registry.register_tool(std::make_unique<tools::ToolSearchTool>());

    auto tool = tools::ToolSearchTool{};
    const auto result =
        tool.execute(nlohmann::json{{"query", "read"}},
                     tools::ToolExecutionContext{
                         .cwd = std::filesystem::current_path(),
                         .tool_registry = &registry,
                     });

    REQUIRE(result.ok());
    CHECK(result->find("read_file:") != std::string::npos);
    CHECK(result->find("write_file:") == std::string::npos);
    CHECK(result->find("grep:") == std::string::npos);

    const auto description_match =
        tool.execute(nlohmann::json{{"query", "overwrite"}},
                     tools::ToolExecutionContext{
                         .cwd = std::filesystem::current_path(),
                         .tool_registry = &registry,
                     });

    REQUIRE(description_match.ok());
    CHECK(description_match->find("write_file:") != std::string::npos);
    CHECK(tool.is_read_only(nlohmann::json::object()));
}

TEST_CASE("tool search is case-insensitive and reports no matches") {
    auto registry = tools::ToolRegistry{};
    registry.register_tool(std::make_unique<tools::GrepTool>());

    auto tool = tools::ToolSearchTool{};
    const auto found =
        tool.execute(nlohmann::json{{"query", "REGULAR"}},
                     tools::ToolExecutionContext{
                         .cwd = std::filesystem::current_path(),
                         .tool_registry = &registry,
                     });
    REQUIRE(found.ok());
    CHECK(found->find("grep:") != std::string::npos);

    const auto missing =
        tool.execute(nlohmann::json{{"query", "definitely_missing"}},
                     tools::ToolExecutionContext{
                         .cwd = std::filesystem::current_path(),
                         .tool_registry = &registry,
                     });
    REQUIRE(missing.ok());
    CHECK(*missing == "(no matches)");
}

TEST_CASE("tool search requires registry context") {
    auto tool = tools::ToolSearchTool{};
    const auto result =
        tool.execute(nlohmann::json{{"query", "file"}},
                     tools::ToolExecutionContext{.cwd = std::filesystem::current_path()});

    REQUIRE_FALSE(result.ok());
    CHECK(result.status().message() == "Tool registry context not available");
}
