#include "codeharness/gateway/stdio_adapter.h"

#include "codeharness/core/json_parse.h"
#include "codeharness/core/strings.h"

#include <nlohmann/json.hpp>
#include <nonstd/expected.hpp>

#include <exception>
#include <string>
#include <utility>

namespace codeharness::gateway
{

namespace
{

constexpr auto adapter_name = "gateway stdio inbound";

auto parse_json_object(std::string_view line) -> Result<nlohmann::json>
{
    try
    {
        auto json = nlohmann::json::parse(std::string{line});
        if (!json.is_object())
        {
            return fail<nlohmann::json>(
                ErrorKind::InvalidArgument,
                "gateway stdio inbound line must be a JSON object");
        }

        return json;
    }
    catch (const nlohmann::json::parse_error& error)
    {
        return fail<nlohmann::json>(
            ErrorKind::InvalidArgument,
            std::string{"gateway stdio inbound line is invalid JSON: "} + error.what());
    }
}

auto read_stdio_string(const nlohmann::json& input, std::string_view field_name) -> Result<std::string>
{
    return read_json_field<std::string>(input, field_name, adapter_name);
}

} // namespace

auto parse_stdio_inbound_line(std::string_view line,
                              std::filesystem::path default_cwd)
    -> Result<GatewayInboundMessage>
{
    auto input = parse_json_object(line);
    if (!input)
    {
        return nonstd::make_unexpected(input.error());
    }

    auto channel = read_stdio_string(*input, "channel");
    if (!channel)
    {
        return nonstd::make_unexpected(channel.error());
    }

    auto conversation_id = read_stdio_string(*input, "conversation_id");
    if (!conversation_id)
    {
        return nonstd::make_unexpected(conversation_id.error());
    }

    auto user_id = read_stdio_string(*input, "user_id");
    if (!user_id)
    {
        return nonstd::make_unexpected(user_id.error());
    }

    auto text = read_stdio_string(*input, "text");
    if (!text)
    {
        return nonstd::make_unexpected(text.error());
    }

    auto cwd = read_nullable_json_field<std::string>(*input, "cwd", adapter_name);
    if (!cwd)
    {
        return nonstd::make_unexpected(cwd.error());
    }

    auto message = GatewayInboundMessage{
        .key = GatewaySessionKey{
            .channel = std::move(*channel),
            .conversation_id = std::move(*conversation_id),
            .user_id = std::move(*user_id),
        },
        .text = std::move(*text),
        .cwd = trim(*cwd).empty() ? std::move(default_cwd) : std::filesystem::path{std::move(*cwd)},
    };

    message.key = normalize_session_key(std::move(message.key));
    auto valid_key = validate_session_key(message.key);
    if (!valid_key)
    {
        return nonstd::make_unexpected(valid_key.error());
    }
    if (trim(message.text).empty())
    {
        return fail<GatewayInboundMessage>(
            ErrorKind::InvalidArgument,
            "gateway stdio inbound text is required");
    }

    return message;
}

auto format_stdio_outbound_line(const GatewayOutboundMessage& message) -> std::string
{
    const auto json = nlohmann::json{
        {"channel", message.key.channel},
        {"conversation_id", message.key.conversation_id},
        {"user_id", message.key.user_id},
        {"text", message.text},
        {"is_error", message.is_error},
    };

    return json.dump() + '\n';
}

GatewayStdioAdapter::GatewayStdioAdapter(GatewayMessageBus& bus, std::filesystem::path default_cwd)
    : bus_{bus}
    , default_cwd_{std::move(default_cwd)}
{
}

auto GatewayStdioAdapter::accept_line(std::string_view line) -> Result<void>
{
    auto message = parse_stdio_inbound_line(line, default_cwd_);
    if (!message)
    {
        return nonstd::make_unexpected(message.error());
    }

    bus_.inbound().push(std::move(*message));
    return {};
}

auto GatewayStdioAdapter::drain_outbound_lines() -> std::vector<std::string>
{
    auto outbound = bus_.outbound().drain();

    std::vector<std::string> lines;
    lines.reserve(outbound.size());
    for (const auto& message : outbound)
    {
        lines.push_back(format_stdio_outbound_line(message));
    }

    return lines;
}

} // namespace codeharness::gateway
