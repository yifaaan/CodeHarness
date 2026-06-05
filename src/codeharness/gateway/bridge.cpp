#include "codeharness/gateway/bridge.h"

#include <nonstd/expected.hpp>

#include <string>
#include <utility>

namespace codeharness::gateway
{

namespace
{

auto make_gateway_error_message(GatewaySessionKey key, const CodeHarnessError& error) -> GatewayOutboundMessage
{
    return GatewayOutboundMessage{
        .key = std::move(key),
        .text = "[gateway error] " + error.message,
        .is_error = true,
    };
}

} // namespace

GatewayBridge::GatewayBridge(GatewayMessageBus& bus, GatewayRuntimePool& runtime_pool)
    : bus_{bus}
    , runtime_pool_{runtime_pool}
{
}

auto GatewayBridge::process_next() -> Result<GatewayBridgeStepResult>
{
    auto inbound = bus_.inbound().try_pop();
    if (!inbound)
    {
        return GatewayBridgeStepResult{};
    }

    auto key = inbound->key;
    auto outbound = runtime_pool_.submit(std::move(*inbound));
    if (!outbound)
    {
        bus_.outbound().push(make_gateway_error_message(std::move(key), outbound.error()));
        return GatewayBridgeStepResult{
            .processed = true,
            .published_error = true,
        };
    }

    bus_.outbound().push(std::move(*outbound));
    return GatewayBridgeStepResult{
        .processed = true,
    };
}

auto GatewayBridge::drain() -> Result<std::size_t>
{
    std::size_t processed = 0;
    while (true)
    {
        auto step = process_next();
        if (!step)
        {
            return nonstd::make_unexpected(step.error());
        }
        if (!step->processed)
        {
            return processed;
        }

        ++processed;
    }
}

} // namespace codeharness::gateway
