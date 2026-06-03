#pragma once

#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace codeharness
{

// Transport configuration is intentionally plain data. Plugin loading, future
// project config loading, and the runtime connection layer all need to pass the
// same facts around without inheriting transport-specific behavior.
//
// Example stdio server:
//   name    = "browser"
//   command = "node"
//   args    = {"server.js"}
//   env     = {{"KEY", "VALUE"}}
//   cwd     = "D:/some/path"
struct McpStdioServerConfig
{
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
    std::optional<std::filesystem::path> cwd;
};

struct McpHttpServerConfig
{
    std::string name;
    std::string url;
    std::map<std::string, std::string> headers;
};

using McpServerConfig = std::variant<McpStdioServerConfig, McpHttpServerConfig>;

// Metadata for one tool exposed by an MCP server. The input schema stays as raw
// JSON Schema because CodeHarness only needs to show it to the model and pass
// user arguments through unchanged.
struct McpToolInfo
{
    std::string server_name;
    std::string name;
    std::string description;
    nlohmann::json input_schema = nlohmann::json::object({{"type", "object"}, {"properties", nlohmann::json::object()}});
};

// Metadata for one resource exposed by an MCP server.
struct McpResourceInfo
{
    std::string server_name;
    std::string name;
    std::string uri;
    std::string description;
};

enum class McpConnectionState
{
    pending,
    connected,
    failed,
    disabled,
};

struct McpConnectionStatus
{
    std::string name;
    McpConnectionState state = McpConnectionState::pending;
    std::string detail;
    std::string transport = "unknown"; // stdio, http, or unknown.
    bool auth_configured = false;
    std::vector<McpToolInfo> tools;
    std::vector<McpResourceInfo> resources;
};

struct McpToolCallResult
{
    std::string content;
    bool is_error = false;
    nlohmann::json raw = nlohmann::json::object();
};

inline auto mcp_server_name(const McpServerConfig& server) -> std::string_view
{
    return std::visit([](const auto& config) -> std::string_view { return config.name; }, server);
}

inline auto mcp_server_transport(const McpServerConfig& server) -> std::string_view
{
    if (std::holds_alternative<McpStdioServerConfig>(server))
    {
        return "stdio";
    }

    return "http";
}

inline auto mcp_connection_state_name(McpConnectionState state) -> std::string_view
{
    switch (state)
    {
    case McpConnectionState::pending: return "pending";
    case McpConnectionState::connected: return "connected";
    case McpConnectionState::failed: return "failed";
    case McpConnectionState::disabled: return "disabled";
    }

    return "unknown";
}

} // namespace codeharness
