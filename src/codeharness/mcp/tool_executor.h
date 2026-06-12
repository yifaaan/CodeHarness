#pragma once

#include "codeharness/core/error.h"
#include "codeharness/mcp/types.h"

#include <nlohmann/json.hpp>

#include <string_view>

namespace codeharness
{

// A narrow interface between normal CodeHarness tools and MCP sessions. The
// adapter does not need to know whether a call is served by one live session, a
// manager with reconnect logic, or a fake executor in tests.
class McpToolExecutor
{
public:
    virtual ~McpToolExecutor() = default;

    virtual auto call_tool(std::string_view server_name, std::string_view tool_name, const nlohmann::json& arguments)
        -> absl::StatusOr<McpToolCallResult> = 0;
};

} // namespace codeharness
