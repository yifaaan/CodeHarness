#include "codeharness/mcp/client_session.h"

#include "codeharness/core/json_parse.h"
#include "codeharness/mcp/json_rpc.h"
#include "codeharness/version.h"

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

auto expect_object_result(const nlohmann::json& result, std::string_view method) -> absl::StatusOr<const nlohmann::json*>
{
    if (!result.is_object())
    {
        return fail<const nlohmann::json*>(absl::UnavailableError , "MCP method " + std::string{method} + " returned a non-object result");
    }

    return &result;
}

auto parse_tool_infos(const nlohmann::json& result, std::string_view server_name) -> absl::StatusOr<std::vector<McpToolInfo>>
{
    auto object = expect_object_result(result, "tools/list");
    if (!object)
    {
        return object.error();
    }

    if (!(*object)->contains("tools"))
    {
        return fail<std::vector<McpToolInfo>>(absl::UnavailableError , "MCP tools/list result requires field: tools");
    }

    //{
    //  "tools": [
    //    {
    //      "name": "read_file",
    //      "description": "Read a file",
    //      "inputSchema": {
    //        "type": "object",
    //        "properties": {
    //          "path": {
    //            "type": "string"
    //          }
    //        }
    //      }
    //    }
    //  ]
    //}
    const auto& tools_json = (*object)->at("tools");
    if (!tools_json.is_array())
    {
        return fail<std::vector<McpToolInfo>>(absl::UnavailableError , "MCP tools/list field tools must be an array");
    }

    std::vector<McpToolInfo> tools;
    tools.reserve(tools_json.size());

    for (const auto& item : tools_json)
    {
        if (!item.is_object())
        {
            return fail<std::vector<McpToolInfo>>(absl::UnavailableError , "MCP tool entry must be an object");
        }

        auto name = ReadJsonField<std::string>(item, "name", "MCP tool entry", {}, absl::UnavailableError );
        if (!name)
        {
            return name.error();
        }

        auto description = ReadJsonField<std::string, JsonFieldMode::kOptionalWithDefault>(item, "description", "MCP tool entry", {}, absl::UnavailableError );
        if (!description)
        {
            return description.error();
        }

        auto input_schema = default_input_schema();
        if (item.contains("inputSchema"))
        {
            if (!item.at("inputSchema").is_object())
            {
                return fail<std::vector<McpToolInfo>>(absl::UnavailableError , "MCP tool inputSchema must be an object: " + *name);
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

auto parse_resource_infos(const nlohmann::json& result, std::string_view server_name) -> absl::StatusOr<std::vector<McpResourceInfo>>
{
    auto object = expect_object_result(result, "resources/list");
    if (!object)
    {
        return object.error();
    }

    if (!(*object)->contains("resources"))
    {
        return fail<std::vector<McpResourceInfo>>(absl::UnavailableError , "MCP resources/list result requires field: resources");
    }

    const auto& resources_json = (*object)->at("resources");
    if (!resources_json.is_array())
    {
        return fail<std::vector<McpResourceInfo>>(absl::UnavailableError , "MCP resources/list field resources must be an array");
    }

    std::vector<McpResourceInfo> resources;
    resources.reserve(resources_json.size());

    for (const auto& item : resources_json)
    {
        if (!item.is_object())
        {
            return fail<std::vector<McpResourceInfo>>(absl::UnavailableError , "MCP resource entry must be an object");
        }

        auto uri = ReadJsonField<std::string>(item, "uri", "MCP resource entry", {}, absl::UnavailableError );
        if (!uri)
        {
            return uri.error();
        }

        auto name = ReadJsonField<std::string, JsonFieldMode::kOptionalWithDefault>(item, "name", "MCP resource entry", *uri, absl::UnavailableError );
        if (!name)
        {
            return name.error();
        }

        auto description = ReadJsonField<std::string, JsonFieldMode::kOptionalWithDefault>(item, "description", "MCP resource entry", {}, absl::UnavailableError );
        if (!description)
        {
            return description.error();
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

auto parse_tool_call_result(const nlohmann::json& result) -> absl::StatusOr<McpToolCallResult>
{
    auto object = expect_object_result(result, "tools/call");
    if (!object)
    {
        return object.error();
    }

    auto parsed = McpToolCallResult{.raw = result};
    auto is_error = ReadJsonField<bool, JsonFieldMode::kOptionalWithDefault>(**object, "isError", "MCP tools/call result", false, absl::UnavailableError );
    if (!is_error)
    {
        return is_error.error();
    }

    parsed.is_error = *is_error;

    if ((*object)->contains("content"))
    {
        const auto& content = (*object)->at("content");
        if (!content.is_array())
        {
            return absl::StatusOr<McpToolCallResult>(absl::UnavailableError("MCP tools/call content must be an array"));
        }

        for (const auto& item : content)
        {
            if (!item.is_object())
            {
                append_part(parsed.content, item.dump());
                continue;
            }

            const auto type = ReadJsonField<std::string, JsonFieldMode::kOptionalIfValid>(item, "type", "MCP content item");
            const auto text = ReadJsonField<std::string, JsonFieldMode::kOptionalIfValid>(item, "text", "MCP content item");
            if (type && *type && **type == "text" && text && *text)
            {
                append_part(parsed.content, **text);
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

auto parse_read_resource_result(const nlohmann::json& result) -> absl::StatusOr<std::string>
{
    auto object = expect_object_result(result, "resources/read");
    if (!object)
    {
        return object.error();
    }

    if (!(*object)->contains("contents"))
    {
        return fail<std::string>(absl::UnavailableError , "MCP resources/read result requires field: contents");
    }

    const auto& contents = (*object)->at("contents");
    if (!contents.is_array())
    {
        return fail<std::string>(absl::UnavailableError , "MCP resources/read contents must be an array");
    }

    std::string output;
    for (const auto& item : contents)
    {
        if (!item.is_object())
        {
            append_part(output, item.dump());
            continue;
        }

        const auto text = ReadJsonField<std::string, JsonFieldMode::kOptionalIfValid>(item, "text", "MCP resource content");
        const auto blob = ReadJsonField<std::string, JsonFieldMode::kOptionalIfValid>(item, "blob", "MCP resource content");
        if (text && *text)
        {
            append_part(output, **text);
        }
        else if (blob && *blob)
        {
            append_part(output, **blob);
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

auto McpClientSession::initialize() -> absl::Status
{
    if (auto started = start_transport(); !started)
    {
        return started.error();
    }

    const auto params = nlohmann::json{
        {"protocolVersion", "2024-11-05"},
        {"capabilities", nlohmann::json::object()},
        {"clientInfo", nlohmann::json{{"name", PROJECT_NAME}, {"version", VERSION}}},
    };

    // 发送 initialize 请求
    if (auto initialized = request("initialize", params); !initialized)
    {
        return initialized.error();
    }

    // 发送 notifications/initialized 通知
    if (auto sent = transport_->send(make_mcp_notification("notifications/initialized")); !sent)
    {
        return sent.error();
    }

    initialized_ = true;
    return {};
}

auto McpClientSession::list_tools(std::string_view server_name) -> absl::StatusOr<std::vector<McpToolInfo>>
{
    if (!initialized_)
    {
        return fail<std::vector<McpToolInfo>>(absl::InternalError , "MCP session is not initialized");
    }

    auto result = request("tools/list", nlohmann::json::object());
    if(!result.ok())
    {
        return result.status();
    }

    return parse_tool_infos(*result, server_name);
}

auto McpClientSession::list_resources(std::string_view server_name) -> absl::StatusOr<std::vector<McpResourceInfo>>
{
    if (!initialized_)
    {
        return fail<std::vector<McpResourceInfo>>(absl::InternalError , "MCP session is not initialized");
    }

    auto response = request_raw("resources/list", nlohmann::json::object());
    if(!response.ok())
    {
        return response.status();
    }

    if (response->error.ok())
    {
        if (is_mcp_method_not_found(*response->error))
        {
            return std::vector<McpResourceInfo>{};
        }

        return fail<std::vector<McpResourceInfo>>(absl::UnavailableError , "MCP method resources/list failed: " + describe_mcp_error(*response->error));
    }

    return parse_resource_infos(*response->result, server_name);
}

auto McpClientSession::call_tool(std::string_view name, const nlohmann::json& arguments) -> absl::StatusOr<McpToolCallResult>
{
    if (!initialized_)
    {
        return absl::StatusOr<McpToolCallResult>(absl::InternalError("MCP session is not initialized"));
    }

    if (!arguments.is_object())
    {
        return absl::StatusOr<McpToolCallResult>(absl::InvalidArgumentError("MCP tool arguments must be a JSON object"));
    }

    auto result = request(
        "tools/call",
        nlohmann::json{
            {"name", std::string{name}},
            {"arguments", arguments},
        });
    if(!result.ok())
    {
        return result.status();
    }

    return parse_tool_call_result(*result);
}

auto McpClientSession::read_resource(std::string_view uri) -> absl::StatusOr<std::string>
{
    if (!initialized_)
    {
        return fail<std::string>(absl::InternalError , "MCP session is not initialized");
    }

    auto result = request("resources/read", nlohmann::json{{"uri", std::string{uri}}});
    if(!result.ok())
    {
        return result.status();
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

auto McpClientSession::start_transport() -> absl::Status
{
    if (started_)
    {
        return {};
    }

    if (auto started = transport_->start(); !started)
    {
        return started.error();
    }

    started_ = true;
    return {};
}

auto McpClientSession::request_raw(std::string_view method, nlohmann::json params) -> absl::StatusOr<McpJsonRpcResponse>
{
    const auto request_id = next_id_;
    ++next_id_;

    if (auto sent = transport_->send(make_mcp_request(request_id, method, std::move(params))); !sent)
    {
        return sent.error();
    }

    while (true)
    {
        auto inbound = transport_->read();
        if (!inbound)
        {
            return inbound.error();
        }

        // Servers may emit notifications between responses. They are useful to
        // UI layers later, but this synchronous session only waits for its own
        // response id, so notifications are safely ignored here.
        // 跳过 notification
        if (is_mcp_notification(*inbound))
        {
            continue;
        }

        auto response = parse_mcp_response(*inbound);
        if(!response.ok())
        {
            return response.status();
        }

        if (response->id != request_id)
        {
            return absl::StatusOr<McpJsonRpcResponse>(absl::UnavailableError("MCP response id mismatch: expected " + std::to_string(request_id)) + ", got " + std::to_string(response->id));
        }

        return response;
    }
}

auto McpClientSession::request(std::string_view method, nlohmann::json params) -> absl::StatusOr<nlohmann::json>
{
    auto response = request_raw(method, std::move(params));
    if(!response.ok())
    {
        return response.status();
    }

    if (response->error.ok())
    {
        return fail<nlohmann::json>(absl::UnavailableError , "MCP method " + std::string{method} + " failed: " + describe_mcp_error(*response->error));
    }

    return *response->result;
}

} // namespace codeharness
