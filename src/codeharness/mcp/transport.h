#pragma once

#include "codeharness/core/result.h"

#include <nlohmann/json.hpp>

namespace codeharness
{

// The MCP protocol is independent from the bytes used to carry it. A stdio
// server, streamable HTTP server, and in-memory fake used by tests can all
// implement this small synchronous interface.
//
// Each method returns Result<T> because transport failures are normal runtime
// failures: a subprocess may exit, a server may send invalid data, or a network
// stream may close. Those failures must be visible to the agent loop instead of
// crashing the harness.
class McpTransport
{
public:
    virtual ~McpTransport() = default;

    virtual auto start() -> Result<void> = 0;
    virtual auto send(const nlohmann::json& message) -> Result<void> = 0;
    virtual auto read() -> Result<nlohmann::json> = 0;
    virtual auto close() noexcept -> void = 0;
};

} // namespace codeharness
