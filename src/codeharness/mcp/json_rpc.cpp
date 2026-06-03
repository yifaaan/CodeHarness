#include "codeharness/mcp/json_rpc.h"

#include <string>
#include <utility>

namespace codeharness
{

namespace
{

constexpr int json_rpc_method_not_found_code = -32601;

auto has_json_rpc_version(const nlohmann::json& message) -> bool
{
    return message.contains("jsonrpc") && message.at("jsonrpc").is_string() &&
           message.at("jsonrpc").get<std::string>() == "2.0";
}

auto parse_rpc_error(const nlohmann::json& error) -> Result<McpJsonRpcError>
{
    if (!error.is_object())
    {
        return fail<McpJsonRpcError>(ErrorKind::Network, "MCP JSON-RPC error must be an object");
    }

    if (!error.contains("code") || !error.at("code").is_number_integer())
    {
        return fail<McpJsonRpcError>(ErrorKind::Network, "MCP JSON-RPC error requires integer field: code");
    }

    if (!error.contains("message") || !error.at("message").is_string())
    {
        return fail<McpJsonRpcError>(ErrorKind::Network, "MCP JSON-RPC error requires string field: message");
    }

    auto parsed = McpJsonRpcError{
        .code = error.at("code").get<int>(),
        .message = error.at("message").get<std::string>(),
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

    if (params.has_value())
    {
        message["params"] = std::move(*params);
    }

    return message;
}

auto is_mcp_notification(const nlohmann::json& message) noexcept -> bool
{
    return message.is_object() && message.contains("method") && !message.contains("id");
}

auto parse_mcp_response(const nlohmann::json& message) -> Result<McpJsonRpcResponse>
{
    if (!message.is_object())
    {
        return fail<McpJsonRpcResponse>(ErrorKind::Network, "MCP JSON-RPC response must be an object");
    }

    if (!has_json_rpc_version(message))
    {
        return fail<McpJsonRpcResponse>(ErrorKind::Network, "MCP JSON-RPC response must declare jsonrpc \"2.0\"");
    }

    if (!message.contains("id") || !message.at("id").is_number_integer())
    {
        return fail<McpJsonRpcResponse>(ErrorKind::Network, "MCP JSON-RPC response requires integer field: id");
    }

    const auto has_result = message.contains("result");
    const auto has_error = message.contains("error");
    if (has_result == has_error)
    {
        return fail<McpJsonRpcResponse>(
            ErrorKind::Network, "MCP JSON-RPC response must contain exactly one of result or error");
    }

    auto response = McpJsonRpcResponse{
        .id = message.at("id").get<int>(),
    };

    if (has_result)
    {
        response.result = message.at("result");
        return response;
    }

    auto error = parse_rpc_error(message.at("error"));
    if (!error)
    {
        return nonstd::make_unexpected(error.error());
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
    if (error.data.has_value())
    {
        text += " data=" + error.data->dump();
    }

    return text;
}

} // namespace codeharness
