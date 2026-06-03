#include "codeharness/mcp/client_session.h"

#include "codeharness/mcp/json_rpc.h"
#include "codeharness/version.h"

#include <nonstd/expected.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace codeharness
{

namespace
{

auto default_input_schema() -> nlohmann::json
{
    return nlohmann::json::object({{"type", "object"}, {"properties", nlohmann::json::object()}});
}

auto expect_object_result(const nlohmann::json& result, std::string_view method) -> Result<const nlohmann::json*>
{
    if (!result.is_object())
    {
        return fail<const nlohmann::json*>(
            ErrorKind::Network, "MCP method " + std::string{method} + " returned a non-object result");
    }

    return &result;
}

auto optional_string_field(const nlohmann::json& object, std::string_view field_name, std::string default_value = {})
    -> Result<std::string>
{
    const auto key = std::string{field_name};
    if (!object.contains(key))
    {
        return default_value;
    }

    if (!object.at(key).is_string())
    {
        return fail<std::string>(ErrorKind::Network, "MCP field must be a string: " + key);
    }

    return object.at(key).get<std::string>();
}

auto required_string_field(const nlohmann::json& object, std::string_view field_name, std::string_view context)
    -> Result<std::string>
{
    const auto key = std::string{field_name};
    if (!object.contains(key) || !object.at(key).is_string())
    {
        return fail<std::string>(ErrorKind::Network, "MCP " + std::string{context} + " requires string field: " + key);
    }

    return object.at(key).get<std::string>();
}

auto parse_tool_infos(const nlohmann::json& result, std::string_view server_name) -> Result<std::vector<McpToolInfo>>
{
    auto object = expect_object_result(result, "tools/list");
    if (!object)
    {
        return nonstd::make_unexpected(object.error());
    }

    if (!(*object)->contains("tools"))
    {
        return fail<std::vector<McpToolInfo>>(ErrorKind::Network, "MCP tools/list result requires field: tools");
    }

    const auto& tools_json = (*object)->at("tools");
    if (!tools_json.is_array())
    {
        return fail<std::vector<McpToolInfo>>(ErrorKind::Network, "MCP tools/list field tools must be an array");
    }

    std::vector<McpToolInfo> tools;
    tools.reserve(tools_json.size());

    for (const auto& item : tools_json)
    {
        if (!item.is_object())
        {
            return fail<std::vector<McpToolInfo>>(ErrorKind::Network, "MCP tool entry must be an object");
        }

        auto name = required_string_field(item, "name", "tool entry");
        if (!name)
        {
            return nonstd::make_unexpected(name.error());
        }

        auto description = optional_string_field(item, "description");
        if (!description)
        {
            return nonstd::make_unexpected(description.error());
        }

        auto input_schema = default_input_schema();
        if (item.contains("inputSchema"))
        {
            if (!item.at("inputSchema").is_object())
            {
                return fail<std::vector<McpToolInfo>>(
                    ErrorKind::Network, "MCP tool inputSchema must be an object: " + *name);
            }

            input_schema = item.at("inputSchema");
        }

        tools.push_back(
            McpToolInfo{
                .server_name = std::string{server_name},
                .name = std::move(*name),
                .description = std::move(*description),
                .input_schema = std::move(input_schema),
            });
    }

    return tools;
}

auto parse_resource_infos(const nlohmann::json& result, std::string_view server_name)
    -> Result<std::vector<McpResourceInfo>>
{
    auto object = expect_object_result(result, "resources/list");
    if (!object)
    {
        return nonstd::make_unexpected(object.error());
    }

    if (!(*object)->contains("resources"))
    {
        return fail<std::vector<McpResourceInfo>>(
            ErrorKind::Network, "MCP resources/list result requires field: resources");
    }

    const auto& resources_json = (*object)->at("resources");
    if (!resources_json.is_array())
    {
        return fail<std::vector<McpResourceInfo>>(
            ErrorKind::Network, "MCP resources/list field resources must be an array");
    }

    std::vector<McpResourceInfo> resources;
    resources.reserve(resources_json.size());

    for (const auto& item : resources_json)
    {
        if (!item.is_object())
        {
            return fail<std::vector<McpResourceInfo>>(ErrorKind::Network, "MCP resource entry must be an object");
        }

        auto uri = required_string_field(item, "uri", "resource entry");
        if (!uri)
        {
            return nonstd::make_unexpected(uri.error());
        }

        auto name = optional_string_field(item, "name", *uri);
        if (!name)
        {
            return nonstd::make_unexpected(name.error());
        }

        auto description = optional_string_field(item, "description");
        if (!description)
        {
            return nonstd::make_unexpected(description.error());
        }

        resources.push_back(
            McpResourceInfo{
                .server_name = std::string{server_name},
                .name = std::move(*name),
                .uri = std::move(*uri),
                .description = std::move(*description),
            });
    }

    return resources;
}

auto append_part(std::string& output, std::string text) -> void
{
    if (!output.empty())
    {
        output += '\n';
    }

    output += std::move(text);
}

auto parse_tool_call_result(const nlohmann::json& result) -> Result<McpToolCallResult>
{
    auto object = expect_object_result(result, "tools/call");
    if (!object)
    {
        return nonstd::make_unexpected(object.error());
    }

    auto parsed = McpToolCallResult{.raw = result};
    if ((*object)->contains("isError"))
    {
        if (!(*object)->at("isError").is_boolean())
        {
            return fail<McpToolCallResult>(ErrorKind::Network, "MCP tools/call isError must be a boolean");
        }

        parsed.is_error = (*object)->at("isError").get<bool>();
    }

    if ((*object)->contains("content"))
    {
        const auto& content = (*object)->at("content");
        if (!content.is_array())
        {
            return fail<McpToolCallResult>(ErrorKind::Network, "MCP tools/call content must be an array");
        }

        for (const auto& item : content)
        {
            if (item.is_object() && item.contains("type") && item.at("type").is_string() &&
                item.at("type").get<std::string>() == "text" && item.contains("text") && item.at("text").is_string())
            {
                append_part(parsed.content, item.at("text").get<std::string>());
            }
            else
            {
                append_part(parsed.content, item.dump());
            }
        }
    }

    // Some MCP servers return structuredContent without textual content. Keep
    // that data visible to the model by serializing it instead of dropping it.
    if (parsed.content.empty() && (*object)->contains("structuredContent"))
    {
        parsed.content = (*object)->at("structuredContent").dump();
    }

    if (parsed.content.empty())
    {
        parsed.content = "(no output)";
    }

    return parsed;
}

auto parse_read_resource_result(const nlohmann::json& result) -> Result<std::string>
{
    auto object = expect_object_result(result, "resources/read");
    if (!object)
    {
        return nonstd::make_unexpected(object.error());
    }

    if (!(*object)->contains("contents"))
    {
        return fail<std::string>(ErrorKind::Network, "MCP resources/read result requires field: contents");
    }

    const auto& contents = (*object)->at("contents");
    if (!contents.is_array())
    {
        return fail<std::string>(ErrorKind::Network, "MCP resources/read contents must be an array");
    }

    std::string output;
    for (const auto& item : contents)
    {
        if (item.is_object() && item.contains("text") && item.at("text").is_string())
        {
            append_part(output, item.at("text").get<std::string>());
        }
        else if (item.is_object() && item.contains("blob") && item.at("blob").is_string())
        {
            append_part(output, item.at("blob").get<std::string>());
        }
        else
        {
            append_part(output, item.dump());
        }
    }

    return output;
}

} // namespace

McpClientSession::McpClientSession(std::unique_ptr<McpTransport> transport) : transport_(std::move(transport))
{
    if (!transport_)
    {
        throw std::invalid_argument{"McpClientSession requires a transport"};
    }
}

McpClientSession::~McpClientSession()
{
    close();
}

auto McpClientSession::initialize() -> Result<void>
{
    if (auto started = start_transport(); !started)
    {
        return nonstd::make_unexpected(started.error());
    }

    const auto params = nlohmann::json{
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"clientInfo", nlohmann::json{{"name", PROJECT_NAME}, {"version", VERSION}}},
    };

    // 发送 initialize 请求
    if (auto initialized = request("initialize", params); !initialized)
    {
        return nonstd::make_unexpected(initialized.error());
    }

    // 发送 notifications/initialized 通知
    if (auto sent = transport_->send(make_mcp_notification("notifications/initialized")); !sent)
    {
        return nonstd::make_unexpected(sent.error());
    }

    initialized_ = true;
    return {};
}

auto McpClientSession::list_tools(std::string_view server_name) -> Result<std::vector<McpToolInfo>>
{
    if (!initialized_)
    {
        return fail<std::vector<McpToolInfo>>(ErrorKind::Internal, "MCP session is not initialized");
    }

    auto result = request("tools/list", nlohmann::json::object());
    if (!result)
    {
        return nonstd::make_unexpected(result.error());
    }

    return parse_tool_infos(*result, server_name);
}

auto McpClientSession::list_resources(std::string_view server_name) -> Result<std::vector<McpResourceInfo>>
{
    if (!initialized_)
    {
        return fail<std::vector<McpResourceInfo>>(ErrorKind::Internal, "MCP session is not initialized");
    }

    auto response = request_raw("resources/list", nlohmann::json::object());
    if (!response)
    {
        return nonstd::make_unexpected(response.error());
    }

    if (response->error.has_value())
    {
        if (is_mcp_method_not_found(*response->error))
        {
            return std::vector<McpResourceInfo>{};
        }

        return fail<std::vector<McpResourceInfo>>(
            ErrorKind::Network, "MCP method resources/list failed: " + describe_mcp_error(*response->error));
    }

    return parse_resource_infos(*response->result, server_name);
}

auto McpClientSession::call_tool(std::string_view name, const nlohmann::json& arguments) -> Result<McpToolCallResult>
{
    if (!initialized_)
    {
        return fail<McpToolCallResult>(ErrorKind::Internal, "MCP session is not initialized");
    }

    if (!arguments.is_object())
    {
        return fail<McpToolCallResult>(ErrorKind::InvalidArgument, "MCP tool arguments must be a JSON object");
    }

    auto result = request(
        "tools/call",
        nlohmann::json{
            {"name", std::string{name}},
            {"arguments", arguments},
        });
    if (!result)
    {
        return nonstd::make_unexpected(result.error());
    }

    return parse_tool_call_result(*result);
}

auto McpClientSession::read_resource(std::string_view uri) -> Result<std::string>
{
    if (!initialized_)
    {
        return fail<std::string>(ErrorKind::Internal, "MCP session is not initialized");
    }

    auto result = request("resources/read", nlohmann::json{{"uri", std::string{uri}}});
    if (!result)
    {
        return nonstd::make_unexpected(result.error());
    }

    return parse_read_resource_result(*result);
}

auto McpClientSession::close() noexcept -> void
{
    if (transport_ != nullptr && started_)
    {
        transport_->close();
        started_ = false;
        initialized_ = false;
    }
}

auto McpClientSession::start_transport() -> Result<void>
{
    if (started_)
    {
        return {};
    }

    
    if (auto started = transport_->start(); !started)
    {
        return nonstd::make_unexpected(started.error());
    }

    started_ = true;
    return {};
}

auto McpClientSession::request_raw(std::string_view method, nlohmann::json params) -> Result<McpJsonRpcResponse>
{
    const auto request_id = next_id_;
    ++next_id_;

    if (auto sent = transport_->send(make_mcp_request(request_id, method, std::move(params))); !sent)
    {
        return nonstd::make_unexpected(sent.error());
    }

    while (true)
    {
        auto inbound = transport_->read();
        if (!inbound)
        {
            return nonstd::make_unexpected(inbound.error());
        }

        // Servers may emit notifications between responses. They are useful to
        // UI layers later, but this synchronous session only waits for its own
        // response id, so notifications are safely ignored here.
        if (is_mcp_notification(*inbound))
        {
            continue;
        }

        auto response = parse_mcp_response(*inbound);
        if (!response)
        {
            return nonstd::make_unexpected(response.error());
        }

        if (response->id != request_id)
        {
            return fail<McpJsonRpcResponse>(
                ErrorKind::Network,
                "MCP response id mismatch: expected " + std::to_string(request_id) + ", got " +
                    std::to_string(response->id));
        }

        return response;
    }
}

auto McpClientSession::request(std::string_view method, nlohmann::json params) -> Result<nlohmann::json>
{
    auto response = request_raw(method, std::move(params));
    if (!response)
    {
        return nonstd::make_unexpected(response.error());
    }

    if (response->error.has_value())
    {
        return fail<nlohmann::json>(
            ErrorKind::Network,
            "MCP method " + std::string{method} + " failed: " + describe_mcp_error(*response->error));
    }

    return *response->result;
}

} // namespace codeharness
