#pragma once

#include "codeharness/core/result.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace codeharness::gateway
{

struct GatewaySessionKey
{
    std::string channel;
    std::string conversation_id;
    std::string user_id;

    auto operator==(const GatewaySessionKey&) const -> bool = default;
};

struct GatewayInboundMessage
{
    GatewaySessionKey key;
    std::string text;
    std::filesystem::path cwd;
};

struct GatewayOutboundMessage
{
    GatewaySessionKey key;
    std::string text;
    bool is_error = false;
};

class GatewayRuntime
{
public:
    virtual ~GatewayRuntime() = default;

    virtual auto submit(GatewayInboundMessage message) -> Result<GatewayOutboundMessage> = 0;
};

using GatewayRuntimeFactory = std::function<Result<std::unique_ptr<GatewayRuntime>>(const GatewaySessionKey& key)>;

auto normalize_session_key(GatewaySessionKey key) -> GatewaySessionKey;
auto session_key_identity(const GatewaySessionKey& key) -> std::string;
auto validate_session_key(const GatewaySessionKey& key) -> Result<void>;

class GatewayRuntimePool
{
public:
    explicit GatewayRuntimePool(GatewayRuntimeFactory factory);

    GatewayRuntimePool(const GatewayRuntimePool&) = delete;
    auto operator=(const GatewayRuntimePool&) -> GatewayRuntimePool& = delete;
    GatewayRuntimePool(GatewayRuntimePool&&) noexcept = default;
    auto operator=(GatewayRuntimePool&&) noexcept -> GatewayRuntimePool& = default;

    auto submit(GatewayInboundMessage message) -> Result<GatewayOutboundMessage>;

    [[nodiscard]] auto active_session_count() const noexcept -> std::size_t;
    [[nodiscard]] auto has_session(const GatewaySessionKey& key) const -> bool;

private:
    GatewayRuntimeFactory factory_;
    std::map<std::string, std::unique_ptr<GatewayRuntime>> runtimes_;
};

} // namespace codeharness::gateway
