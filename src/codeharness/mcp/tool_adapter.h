#pragma once

#include "codeharness/mcp/tool_executor.h"
#include "codeharness/mcp/types.h"
#include "codeharness/tools/tool.h"

#include <nlohmann/json.hpp>

#include <string>

namespace codeharness
{

// Wraps one MCP server tool as a normal CodeHarness Tool. The engine can then
// keep using ToolRegistry without learning a second execution path.
class McpToolAdapter final : public Tool
{
public:
    McpToolAdapter(McpToolExecutor& executor, McpToolInfo tool_info);

    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto is_read_only() const noexcept -> bool override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> absl::StatusOr<ToolResponse> override;

    auto server_name() const -> const std::string&;
    auto tool_name() const -> const std::string&;
    auto input_schema() const -> const nlohmann::json&;

private:
    McpToolExecutor* executor_ = nullptr;
    McpToolInfo tool_info_;
    std::string adapted_name_;
};

auto make_mcp_tool_name(std::string_view server_name, std::string_view tool_name) -> std::string;

} // namespace codeharness
