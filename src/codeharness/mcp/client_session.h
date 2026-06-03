#pragma once

#include "codeharness/core/result.h"
#include "codeharness/mcp/json_rpc.h"
#include "codeharness/mcp/transport.h"
#include "codeharness/mcp/types.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <string_view>
#include <vector>

namespace codeharness
{

// Owns one JSON-RPC conversation with one MCP server. The class deliberately
// stays synchronous for the first C++ implementation: it is small, testable, and
// still matches the request/response flow required by MCP initialize, list, and
// tool calls. A future async version can keep the public data model and replace
// the transport scheduling underneath.
// Responsibilities include:
// 启动 MCP transport
// 发送 initialize
// 发送 tools/list、tools/call、resources/list、resources/read
// 读取 JSON-RPC 响应
// 解析成 CodeHarness 自己的数据结构
class McpClientSession
{
public:
    explicit McpClientSession(std::unique_ptr<McpTransport> transport);
    ~McpClientSession();

    McpClientSession(const McpClientSession&) = delete;
    auto operator=(const McpClientSession&) -> McpClientSession& = delete;
    McpClientSession(McpClientSession&&) noexcept = default;
    auto operator=(McpClientSession&&) noexcept -> McpClientSession& = default;

    auto initialize() -> Result<void>;
    auto list_tools(std::string_view server_name) -> Result<std::vector<McpToolInfo>>;
    auto list_resources(std::string_view server_name) -> Result<std::vector<McpResourceInfo>>;
    auto call_tool(std::string_view name, const nlohmann::json& arguments) -> Result<McpToolCallResult>;
    auto read_resource(std::string_view uri) -> Result<std::string>;
    auto close() noexcept -> void;

private:
    auto start_transport() -> Result<void>;
    auto request_raw(std::string_view method, nlohmann::json params) -> Result<McpJsonRpcResponse>;
    auto request(std::string_view method, nlohmann::json params) -> Result<nlohmann::json>;

    std::unique_ptr<McpTransport> transport_; // 发 JSON、收 JSON
    int next_id_ = 1;
    bool started_ = false;
    bool initialized_ = false;
};

} // namespace codeharness
