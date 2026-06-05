#include "codeharness/gateway/runtime_pool.h"

#include "codeharness/core/strings.h"

#include <nonstd/expected.hpp>

#include <exception>
#include <string>
#include <string_view>
#include <utility>

namespace codeharness::gateway
{

namespace
{

auto trimmed_string(std::string_view value) -> std::string
{
    return std::string{trim(value)};
}

auto append_identity_part(std::string& output, std::string_view value) -> void
{
    output += std::to_string(value.size());
    output += ':';
    output += value;
}

auto validate_inbound_message(const GatewayInboundMessage& message) -> Result<void>
{
    auto valid_key = validate_session_key(message.key);
    if (!valid_key)
    {
        return nonstd::make_unexpected(valid_key.error());
    }

    if (trim(message.text).empty())
    {
        return fail<void>(ErrorKind::InvalidArgument, "gateway inbound text is required");
    }

    return {};
}

auto submit_to_runtime(GatewayRuntime& runtime, GatewayInboundMessage message)
    -> Result<GatewayOutboundMessage>
{
    try
    {
        return runtime.submit(std::move(message));
    }
    catch (const std::exception& error)
    {
        return fail<GatewayOutboundMessage>(
            ErrorKind::Internal,
            std::string{"gateway runtime submit threw: "} + error.what());
    }
    catch (...)
    {
        return fail<GatewayOutboundMessage>(
            ErrorKind::Internal,
            "gateway runtime submit threw an unknown exception");
    }
}

} // namespace

auto normalize_session_key(GatewaySessionKey key) -> GatewaySessionKey
{
    key.channel = trimmed_string(key.channel);
    key.conversation_id = trimmed_string(key.conversation_id);
    key.user_id = trimmed_string(key.user_id);
    return key;
}

auto session_key_identity(const GatewaySessionKey& key) -> std::string
{
    const auto normalized = normalize_session_key(key);

    std::string identity;
    identity.reserve(
        normalized.channel.size() + normalized.conversation_id.size() + normalized.user_id.size() + 24);
    append_identity_part(identity, normalized.channel);
    identity += '|';
    append_identity_part(identity, normalized.conversation_id);
    identity += '|';
    append_identity_part(identity, normalized.user_id);
    return identity;
}

auto validate_session_key(const GatewaySessionKey& key) -> Result<void>
{
    const auto normalized = normalize_session_key(key);
    if (normalized.channel.empty())
    {
        return fail<void>(ErrorKind::InvalidArgument, "gateway session channel is required");
    }
    if (normalized.conversation_id.empty())
    {
        return fail<void>(ErrorKind::InvalidArgument, "gateway session conversation_id is required");
    }
    if (normalized.user_id.empty())
    {
        return fail<void>(ErrorKind::InvalidArgument, "gateway session user_id is required");
    }

    return {};
}

GatewayRuntimePool::GatewayRuntimePool(GatewayRuntimeFactory factory)
    : factory_{std::move(factory)}
{
}

auto GatewayRuntimePool::submit(GatewayInboundMessage message) -> Result<GatewayOutboundMessage>
{
    message.key = normalize_session_key(std::move(message.key));
    message.text = trimmed_string(message.text);

    auto valid_message = validate_inbound_message(message);
    if (!valid_message)
    {
        return nonstd::make_unexpected(valid_message.error());
    }

    const auto identity = session_key_identity(message.key);
    auto runtime = runtimes_.find(identity);
    if (runtime == runtimes_.end())
    {
        if (!factory_)
        {
            return fail<GatewayOutboundMessage>(
                ErrorKind::Config,
                "gateway runtime factory is not configured");
        }

        Result<std::unique_ptr<GatewayRuntime>> created;
        try
        {
            created = factory_(message.key);
        }
        catch (const std::exception& error)
        {
            return fail<GatewayOutboundMessage>(
                ErrorKind::Internal,
                std::string{"gateway runtime factory threw: "} + error.what());
        }
        catch (...)
        {
            return fail<GatewayOutboundMessage>(
                ErrorKind::Internal,
                "gateway runtime factory threw an unknown exception");
        }

        if (!created)
        {
            return nonstd::make_unexpected(created.error());
        }
        if (!*created)
        {
            return fail<GatewayOutboundMessage>(
                ErrorKind::Internal,
                "gateway runtime factory returned null runtime");
        }

        runtime = runtimes_.emplace(identity, std::move(*created)).first;
    }

    return submit_to_runtime(*runtime->second, std::move(message));
}

auto GatewayRuntimePool::active_session_count() const noexcept -> std::size_t
{
    return runtimes_.size();
}

auto GatewayRuntimePool::has_session(const GatewaySessionKey& key) const -> bool
{
    return runtimes_.contains(session_key_identity(key));
}

} // namespace codeharness::gateway
