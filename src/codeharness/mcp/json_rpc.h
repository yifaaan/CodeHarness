#pragma once

#include "codeharness/core/error.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace codeharness
{

struct McpJsonRpcError
{
    int code = 0;
    std::string message;
    std::optional<nlohmann::json> data;
};

struct McpJsonRpcResponse
{
    int id = 0;
    std::optional<nlohmann::json> result;
    std::optional<McpJsonRpcError> error;
};

// {
//      "jsonrpc": "2.0",
//      "id": 8,
//      "method": "tools/call",
//      "params": {
//              "name": "read_file",
//              "arguments": {
//                  "path": "README.md"
//              }
//      }
// }
auto make_mcp_request(int id, std::string_view method, nlohmann::json params = nlohmann::json::object())
    -> nlohmann::json;

// {
//   "jsonrpc": "2.0",
//   "method": "notifications/message",
//   "params": {
//     "level": "info",
//     "message": "ready"
//   }
// }
auto make_mcp_notification(std::string_view method, std::optional<nlohmann::json> params = std::nullopt)
    -> nlohmann::json;

auto is_mcp_notification(const nlohmann::json& message) noexcept -> bool;

auto parse_mcp_response(const nlohmann::json& message) -> absl::StatusOr<McpJsonRpcResponse>;

auto is_mcp_method_not_found(const McpJsonRpcError& error) noexcept -> bool;

auto describe_mcp_error(const McpJsonRpcError& error) -> std::string;

} // namespace codeharness
