#pragma once

#include "codeharness/mcp/transport.h"
#include "codeharness/mcp/types.h"

#include <reproc++/reproc.hpp>

#include <string>

namespace codeharness
{

struct McpStdioTransportOptions
{
    int io_timeout_ms = 30000;
    int stop_timeout_ms = 5000;
};

// JSON-line transport for stdio MCP servers. It owns one child process and
// sends/receives one JSON-RPC message per line:
//
//   parent stdin  -> server stdin
//   server stdout -> parent stdout parser
//   server stderr -> log only, never parsed as protocol data
//
// Keeping stderr out of the protocol stream matters because many MCP servers
// log diagnostics there while still returning valid JSON-RPC on stdout.
class McpStdioTransport final : public McpTransport
{
public:
    explicit McpStdioTransport(McpStdioServerConfig config, McpStdioTransportOptions options = {});
    ~McpStdioTransport() override;

    McpStdioTransport(const McpStdioTransport&) = delete;
    auto operator=(const McpStdioTransport&) -> McpStdioTransport& = delete;
    McpStdioTransport(McpStdioTransport&&) = delete;
    auto operator=(McpStdioTransport&&) -> McpStdioTransport& = delete;

    auto start() -> absl::Status override;
    auto send(const nlohmann::json& message) -> absl::Status override;
    auto read() -> absl::StatusOr<nlohmann::json> override;
    auto close() noexcept -> void override;

private:
    auto read_available(reproc::stream stream, std::string& buffer) -> absl::Status;
    auto next_stdout_line() -> std::optional<std::string>;
    auto log_stderr_lines() -> void;

    McpStdioServerConfig config_;
    McpStdioTransportOptions options_;
    reproc::process process_;
    std::string stdout_buffer_;
    std::string stderr_buffer_;
    bool running_ = false;
};

} // namespace codeharness
