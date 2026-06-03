#include "codeharness/mcp/json_rpc.h"

#include "codeharness/core/json_parse.h"

#include <nonstd/expected.hpp>

#include <string>
#include <utility>

namespace codeharness
{

namespace
{

constexpr int json_rpc_method_not_found_code = -32601;

auto parse_rpc_error(const nlohmann::json& error) -> Result<McpJsonRpcError>
{
    if (!error.is_object())
    {
        return fail<McpJsonRpcError>(ErrorKind::Network, "MCP JSON-RPC error must be an object");
    }

    auto code = read_json_field<int>(error, "code", "MCP JSON-RPC error", {}, ErrorKind::Network);
    if (!code)
    {
        return nonstd::make_unexpected(code.error());
    }

    auto message = read_json_field<std::string>(error, "message", "MCP JSON-RPC error", {}, ErrorKind::Network);
    if (!message)
    {
        return nonstd::make_unexpected(message.error());
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

    auto version = read_json_field<std::string>(message, "jsonrpc", "MCP JSON-RPC response", {}, ErrorKind::Network);
    if (!version)
    {
        return nonstd::make_unexpected(version.error());
    }

    if (*version != "2.0")
    {
        return fail<McpJsonRpcResponse>(ErrorKind::Network, "MCP JSON-RPC response must declare jsonrpc \"2.0\"");
    }

    auto id = read_json_field<int>(message, "id", "MCP JSON-RPC response", {}, ErrorKind::Network);
    if (!id)
    {
        return nonstd::make_unexpected(id.error());
    }

    const auto has_result = message.contains("result");
    const auto has_error = message.contains("error");
    if (has_result == has_error)
    {
        return fail<McpJsonRpcResponse>(ErrorKind::Network, "MCP JSON-RPC response must contain exactly one of result or error");
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
