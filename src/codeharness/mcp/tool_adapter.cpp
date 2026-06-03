#include "codeharness/mcp/tool_adapter.h"

#include <nonstd/expected.hpp>

#include <cctype>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace codeharness
{

namespace
{

auto sanitize_tool_segment(std::string_view value) -> std::string
{
    std::string sanitized;
    sanitized.reserve(value.size());

    std::ranges::transform(value, std::back_inserter(sanitized), [](char ch) {
        if (std::isalnum(ch) != 0 || ch == '_' || ch == '-')
        {
            return ch;
        }
        return '_';
    });

    // Provider tool names commonly require a letter at the front. Prefixing
    // keeps numeric server names usable while making the generated name stable.
    if (sanitized.empty())
    {
        return "tool";
    }

    if (std::isalpha(sanitized.front()) == 0)
    {
        return "mcp_" + sanitized;
    }

    return sanitized;
}

auto adapter_input_json(const ToolRequest& request) -> Result<nlohmann::json>
{
    if (!request.parsed_input.is_null())
    {
        if (!request.parsed_input.is_object())
        {
            return fail<nlohmann::json>(ErrorKind::InvalidArgument, "MCP tool input must be a JSON object");
        }

        return request.parsed_input;
    }

    try
    {
        auto parsed = nlohmann::json::parse(request.input_json);
        if (!parsed.is_object())
        {
            return fail<nlohmann::json>(ErrorKind::InvalidArgument, "MCP tool input must be a JSON object");
        }

        return parsed;
    }
    catch (const nlohmann::json::parse_error& error)
    {
        return fail<nlohmann::json>(ErrorKind::InvalidArgument, "MCP tool input is not valid JSON: " + std::string{error.what()});
    }
}

} // namespace

McpToolAdapter::McpToolAdapter(McpToolExecutor& executor, McpToolInfo tool_info) :
    executor_(&executor), tool_info_(std::move(tool_info)), adapted_name_(make_mcp_tool_name(tool_info_.server_name, tool_info_.name))
{
    if (tool_info_.server_name.empty())
    {
        throw std::invalid_argument{"MCP tool adapter requires a server name"};
    }

    if (tool_info_.name.empty())
    {
        throw std::invalid_argument{"MCP tool adapter requires a tool name"};
    }
}

auto McpToolAdapter::name() const -> std::string
{
    return adapted_name_;
}

auto McpToolAdapter::description() const -> std::string
{
    if (!tool_info_.description.empty())
    {
        return tool_info_.description;
    }

    return "MCP tool " + tool_info_.name + " from server " + tool_info_.server_name + ".";
}

auto McpToolAdapter::is_read_only() const noexcept -> bool
{
    return false;
}

auto McpToolAdapter::execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>
{
    auto arguments = adapter_input_json(request);
    if (!arguments)
    {
        return ToolResponse{
            .tool_use_id = request.id,
            .content = arguments.error().message,
            .is_error = true,
        };
    }

    auto result = executor_->call_tool(tool_info_.server_name, tool_info_.name, *arguments);
    if (!result)
    {
        return ToolResponse{
            .tool_use_id = request.id,
            .content = "MCP tool " + tool_info_.name + " failed: " + result.error().message,
            .is_error = true,
        };
    }

    return ToolResponse{
        .tool_use_id = request.id,
        .content = std::move(result->content),
        .is_error = result->is_error,
    };
}

auto McpToolAdapter::server_name() const -> const std::string&
{
    return tool_info_.server_name;
}

auto McpToolAdapter::tool_name() const -> const std::string&
{
    return tool_info_.name;
}

auto McpToolAdapter::input_schema() const -> const nlohmann::json&
{
    return tool_info_.input_schema;
}

auto make_mcp_tool_name(std::string_view server_name, std::string_view tool_name) -> std::string
{
    return "mcp__" + sanitize_tool_segment(server_name) + "__" + sanitize_tool_segment(tool_name);
}

} // namespace codeharness
