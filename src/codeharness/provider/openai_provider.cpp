#include "codeharness/provider/openai_provider.h"
#include "codeharness/provider/openai_serialize.h"
#include "codeharness/provider/openai_stream_parser.h"
#include "codeharness/provider/provider_http.h"

#include "codeharness/core/log.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>

namespace codeharness
{

namespace
{

constexpr std::string_view kDefaultOpenAIModel = "gpt-4o";
constexpr std::string_view kDefaultOpenAIResponsesUrl = "https://api.openai.com/v1/responses";

} // namespace

OpenAIProvider::OpenAIProvider(ProviderConfig config,
                               std::vector<std::pair<std::string, std::string>> tool_descriptions)
    : config_{std::move(config)}
    , tool_descriptions_{std::move(tool_descriptions)}
{
}

auto OpenAIProvider::stream(std::span<const Message> messages, const ProviderEventSink& sink) -> Result<void>
{
    const auto url = provider_endpoint_url(config_.base_url, "responses", kDefaultOpenAIResponsesUrl);

    nlohmann::json body;
    body["model"] = config_.model.empty() ? std::string{kDefaultOpenAIModel} : config_.model;
    body["input"] = serialize_openai_input(messages);
    body["stream"] = true;
    body["max_output_tokens"] = 4096;

    if (!tool_descriptions_.empty())
    {
        body["tools"] = serialize_openai_tools(tool_descriptions_);
    }

    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    headers["Accept"] = "text/event-stream";
    headers["Authorization"] = "Bearer " + config_.api_key;

    auto body_str = body.dump();

    spdlog::debug("OpenAI request: POST {} ({} bytes)", url, body_str.size());

    OpenAIStreamParser stream_parser;
    std::string stream_error;

    auto response = provider_http_post_with_retry("OpenAI", [&]() -> Result<network::HttpResponse> {
        // Reset stream-parser state so a fresh attempt starts clean.
        stream_parser = OpenAIStreamParser{};
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
        return fail<void>(ErrorKind::Network, "OpenAI HTTP request failed: " + response.error().message);
    }

    if (response->status_code != 200)
    {
        return fail<void>(ErrorKind::Provider, provider_http_error_message("OpenAI", *response));
    }

    if (!stream_error.empty())
    {
        return fail<void>(ErrorKind::Provider, std::move(stream_error));
    }

    return {};
}

} // namespace codeharness
