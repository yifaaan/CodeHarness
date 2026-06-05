#include "codeharness/gateway/service.h"

#include <utility>

namespace codeharness::gateway
{

GatewayService::GatewayService(GatewayRuntimeFactory factory, std::filesystem::path default_cwd)
    : bus_{}
    , runtime_pool_{std::move(factory)}
    , bridge_{bus_, runtime_pool_}
    , stdio_adapter_{bus_, std::move(default_cwd)}
{
}

GatewayService::~GatewayService() = default;

auto GatewayService::accept_stdio_line(std::string_view line) -> Result<void>
{
    return stdio_adapter_.accept_line(line);
}

auto GatewayService::process_next() -> Result<GatewayBridgeStepResult>
{
    return bridge_.process_next();
}

auto GatewayService::drain_inbound() -> Result<std::size_t>
{
    return bridge_.drain();
}

auto GatewayService::drain_outbound_lines() -> std::vector<std::string>
{
    return stdio_adapter_.drain_outbound_lines();
}

auto GatewayService::bus() noexcept -> GatewayMessageBus&
{
    return bus_;
}

auto GatewayService::bus() const noexcept -> const GatewayMessageBus&
{
    return bus_;
}

auto GatewayService::runtime_pool() noexcept -> GatewayRuntimePool&
{
    return runtime_pool_;
}

auto GatewayService::runtime_pool() const noexcept -> const GatewayRuntimePool&
{
    return runtime_pool_;
}

} // namespace codeharness::gateway
