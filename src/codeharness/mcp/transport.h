#pragma once

#include "codeharness/core/error.h"

#include <nlohmann/json.hpp>

namespace codeharness
{

// The MCP protocol is independent from the bytes used to carry it. A stdio
// server, streamable HTTP server, and in-memory fake used by tests can all
// implement this small synchronous interface.
//
// Each method returns absl::StatusOr<T> because transport failures are normal runtime
// failures: a subprocess may exit, a server may send invalid data, or a network
// stream may close. Those failures must be visible to the agent loop instead of
// crashing the harness.
class McpTransport
{
public:
    virtual ~McpTransport() = default;

    virtual auto start() -> absl::Status = 0;
    virtual auto send(const nlohmann::json& message) -> absl::Status = 0;
    virtual auto read() -> absl::StatusOr<nlohmann::json> = 0;
    virtual auto close() noexcept -> void = 0;
};

} // namespace codeharness
