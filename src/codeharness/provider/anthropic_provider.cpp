#include "codeharness/provider/anthropic_provider.h"
#include "codeharness/provider/anthropic_serialize.h"
#include "codeharness/provider/anthropic_stream_parser.h"
#include "codeharness/provider/provider_http.h"

#include "codeharness/core/log.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>

namespace codeharness
{

namespace
{

constexpr std::string_view kDefaultAnthropicModel = "claude-sonnet-4-20250514";
constexpr std::string_view kDefaultAnthropicMessagesUrl = "https://api.anthropic.com/v1/messages";
constexpr std::string_view kAnthropicVersion = "2023-06-01";

} // namespace

AnthropicProvider::AnthropicProvider(ProviderConfig config,
                                     std::vector<std::pair<std::string, std::string>> tool_descriptions) :
    config_{std::move(config)}, tool_descriptions_{std::move(tool_descriptions)}
{
}

auto AnthropicProvider::stream(std::span<const Message> messages, const ProviderEventSink& sink) -> Result<void>
{
    const auto url = provider_endpoint_url(config_.base_url, "messages", kDefaultAnthropicMessagesUrl);

    nlohmann::json body;
    body["model"] = config_.model.empty() ? std::string{kDefaultAnthropicModel} : config_.model;
    body["messages"] = serialize_anthropic_messages(messages);
    body["stream"] = true;
    body["max_tokens"] = 4096;

    auto system = serialize_anthropic_system(messages);
    if (!system.empty())
    {
        body["system"] = std::move(system);
    }

    if (!tool_descriptions_.empty())
    {
        body["tools"] = serialize_anthropic_tools(tool_descriptions_);
    }

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    headers["Accept"] = "text/event-stream";
    headers["x-api-key"] = config_.api_key;
    headers["anthropic-version"] = std::string{kAnthropicVersion};

    auto body_str = body.dump();
    spdlog::debug("Anthropic request: POST {} ({} bytes)", url, body_str.size());

    AnthropicStreamParser stream_parser;
    std::string stream_error;

    auto response = provider_http_post_with_retry("Anthropic", [&]() -> Result<network::HttpResponse> {
        // Reset stream-parser state so a fresh attempt starts clean.
        stream_parser = AnthropicStreamParser{};
        stream_error.clear();

        return http_.post(url, headers, body_str, [&](std::string_view chunk) {
            auto parsed = stream_parser.feed(chunk);
            if (!parsed.error.empty() && stream_error.empty())
            {
                stream_error = std::move(parsed.error);
            }
            for (const auto& event : parsed.events)
            {
                sink(event);
            }
        });
    });

    if (!response)
    {
        return fail<void>(ErrorKind::Network, "Anthropic HTTP request failed: " + response.error().message);
    }

    if (response->status_code != 200)
    {
        return fail<void>(ErrorKind::Provider, provider_http_error_message("Anthropic", *response));
    }

    if (!stream_error.empty())
    {
        return fail<void>(ErrorKind::Provider, std::move(stream_error));
    }

    return {};
}

} // namespace codeharness
