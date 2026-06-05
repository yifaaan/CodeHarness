#pragma once

#include "codeharness/core/result.h"
#include "codeharness/gateway/message_bus.h"
#include "codeharness/gateway/runtime_pool.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness::gateway
{

auto parse_stdio_inbound_line(std::string_view line,
                              std::filesystem::path default_cwd)
    -> Result<GatewayInboundMessage>;
auto format_stdio_outbound_line(const GatewayOutboundMessage& message) -> std::string;

class GatewayStdioAdapter
{
public:
    GatewayStdioAdapter(GatewayMessageBus& bus, std::filesystem::path default_cwd);

    GatewayStdioAdapter(const GatewayStdioAdapter&) = delete;
    auto operator=(const GatewayStdioAdapter&) -> GatewayStdioAdapter& = delete;

    auto accept_line(std::string_view line) -> Result<void>;
    auto drain_outbound_lines() -> std::vector<std::string>;

private:
    GatewayMessageBus& bus_;
    std::filesystem::path default_cwd_;
};

} // namespace codeharness::gateway
