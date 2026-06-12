#include "codeharness/mcp/json_rpc.h"

#include "codeharness/core/json_parse.h"

#include <string>
#include <utility>

namespace codeharness
{

namespace
{

constexpr int json_rpc_method_not_found_code = -32601;

auto parse_rpc_error(const nlohmann::json& error) -> absl::StatusOr<McpJsonRpcError>
{
    if (!error.is_object())
    {
        return absl::StatusOr<McpJsonRpcError>(absl::UnavailableError("MCP JSON-RPC error must be an object"));
    }

    auto code = ReadJsonField<int>(error, "code", "MCP JSON-RPC error", {}, absl::UnavailableError );
    if (!code)
    {
        return code.error();
    }

    auto message = ReadJsonField<std::string>(error, "message", "MCP JSON-RPC error", {}, absl::UnavailableError );
    if (!message)
    {
        return message.error();
    }

    auto parsed = McpJsonRpcError{
        .code = *code,
        .message = std::move(*message),
    };

    if (error.contains("data"))
    {
        parsed.data = error.at("data");
    }

    return parsed;
}

} // namespace

auto make_mcp_request(int id, std::string_view method, nlohmann::json params) -> nlohmann::json
{
    // MCP uses JSON-RPC 2.0. We always use numeric ids because the synchronous
    // client session only has one id source and can cheaply match responses.

    return nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", std::string{method}},
        {"params", std::move(params)},
    };
}

auto make_mcp_notification(std::string_view method, std::optional<nlohmann::json> params) -> nlohmann::json
{
    auto message = nlohmann::json{
        {"jsonrpc", "2.0"},
        {"method", std::string{method}},
    };

    if (params.ok())
    {
        message["params"] = std::move(*params);
    }

    return message;
}

auto is_mcp_notification(const nlohmann::json& message) noexcept -> bool
{
    return message.is_object() && message.contains("method") && !message.contains("id");
}

auto parse_mcp_response(const nlohmann::json& message) -> absl::StatusOr<McpJsonRpcResponse>
{
    if (!message.is_object())
    {
        return absl::StatusOr<McpJsonRpcResponse>(absl::UnavailableError("MCP JSON-RPC response must be an object"));
    }

    auto version = ReadJsonField<std::string>(message, "jsonrpc", "MCP JSON-RPC response", {}, absl::UnavailableError );
    if (!version)
    {
        return version.error();
    }

    if (*version != "2.0")
    {
        return absl::StatusOr<McpJsonRpcResponse>(absl::UnavailableError("MCP JSON-RPC response must declare jsonrpc \"2.0\""));
    }

    auto id = ReadJsonField<int>(message, "id", "MCP JSON-RPC response", {}, absl::UnavailableError );
    if (!id)
    {
        return id.error();
    }

    const auto has_result = message.contains("result");
    const auto has_error = message.contains("error");
    if (has_result == has_error)
    {
        return absl::StatusOr<McpJsonRpcResponse>(absl::UnavailableError("MCP JSON-RPC response must contain exactly one of result or error"));
    }

    auto response = McpJsonRpcResponse{
        .id = *id,
    };

    if (has_result)
    {
        response.result = message.at("result");
        return response;
    }

    auto error = parse_rpc_error(message.at("error"));
    if (!error)
    {
        return error.error();
    }

    response.error = std::move(*error);
    return response;
}

auto is_mcp_method_not_found(const McpJsonRpcError& error) noexcept -> bool
{
    return error.code == json_rpc_method_not_found_code;
}

auto describe_mcp_error(const McpJsonRpcError& error) -> std::string
{
    auto text = std::to_string(error.code) + ": " + error.message;
    if (error.data.ok())
    {
        text += " data=" + error.data->dump();
    }

    return text;
}

} // namespace codeharness
