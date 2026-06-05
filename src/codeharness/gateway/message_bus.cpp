#include "codeharness/gateway/message_bus.h"

namespace codeharness::gateway
{

GatewayMessageBus::~GatewayMessageBus() = default;
GatewayMessageBus::GatewayMessageBus(GatewayMessageBus&&) noexcept = default;
auto GatewayMessageBus::operator=(GatewayMessageBus&&) noexcept -> GatewayMessageBus& = default;

auto GatewayMessageBus::inbound() noexcept -> GatewayMessageQueue<GatewayInboundMessage>&
{
    return inbound_;
}

auto GatewayMessageBus::inbound() const noexcept -> const GatewayMessageQueue<GatewayInboundMessage>&
{
    return inbound_;
}

auto GatewayMessageBus::outbound() noexcept -> GatewayMessageQueue<GatewayOutboundMessage>&
{
    return outbound_;
}

auto GatewayMessageBus::outbound() const noexcept -> const GatewayMessageQueue<GatewayOutboundMessage>&
{
    return outbound_;
}

} // namespace codeharness::gateway
