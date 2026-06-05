#pragma once

#include "codeharness/core/result.h"
#include "codeharness/gateway/message_bus.h"
#include "codeharness/gateway/runtime_pool.h"

#include <cstddef>

namespace codeharness::gateway
{

struct GatewayBridgeStepResult
{
    bool processed = false;
    bool published_error = false;
};

class GatewayBridge
{
public:
    GatewayBridge(GatewayMessageBus& bus, GatewayRuntimePool& runtime_pool);

    GatewayBridge(const GatewayBridge&) = delete;
    auto operator=(const GatewayBridge&) -> GatewayBridge& = delete;

    auto process_next() -> Result<GatewayBridgeStepResult>;
    auto drain() -> Result<std::size_t>;

private:
    GatewayMessageBus& bus_;
    GatewayRuntimePool& runtime_pool_;
};

} // namespace codeharness::gateway
