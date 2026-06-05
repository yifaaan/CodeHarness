#pragma once

#include "codeharness/core/result.h"
#include "codeharness/gateway/bridge.h"
#include "codeharness/gateway/message_bus.h"
#include "codeharness/gateway/runtime_pool.h"
#include "codeharness/gateway/stdio_adapter.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness::gateway
{

class GatewayService
{
public:
    GatewayService(GatewayRuntimeFactory factory, std::filesystem::path default_cwd);
    ~GatewayService();

    GatewayService(const GatewayService&) = delete;
    auto operator=(const GatewayService&) -> GatewayService& = delete;
    GatewayService(GatewayService&&) = delete;
    auto operator=(GatewayService&&) -> GatewayService& = delete;

    auto accept_stdio_line(std::string_view line) -> Result<void>;
    auto process_next() -> Result<GatewayBridgeStepResult>;
    auto drain_inbound() -> Result<std::size_t>;
    auto drain_outbound_lines() -> std::vector<std::string>;

    [[nodiscard]] auto bus() noexcept -> GatewayMessageBus&;
    [[nodiscard]] auto bus() const noexcept -> const GatewayMessageBus&;
    [[nodiscard]] auto runtime_pool() noexcept -> GatewayRuntimePool&;
    [[nodiscard]] auto runtime_pool() const noexcept -> const GatewayRuntimePool&;

private:
    GatewayMessageBus bus_;
    GatewayRuntimePool runtime_pool_;
    GatewayBridge bridge_;
    GatewayStdioAdapter stdio_adapter_;
};

} // namespace codeharness::gateway
