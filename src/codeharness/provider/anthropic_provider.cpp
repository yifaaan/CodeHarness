#include "codeharness/provider/anthropic_provider.h"

#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "codeharness/core/log.h"
#include "codeharness/provider/anthropic_serialize.h"
#include "codeharness/provider/anthropic_stream_parser.h"
#include "codeharness/provider/provider_http.h"

namespace codeharness {

namespace {

constexpr std::string_view kDefaultAnthropicModel = "claude-sonnet-4-20250514";
constexpr std::string_view kDefaultAnthropicMessagesUrl = "https://api.anthropic.com/v1/messages";
constexpr std::string_view kAnthropicVersion = "2023-06-01";

}  // namespace

AnthropicProvider::AnthropicProvider(ProviderConfig config,
                                     std::vector<std::pair<std::string, std::string>> tool_descriptions)
    : config_{std::move(config)}, tool_descriptions_{std::move(tool_descriptions)} {}

std::string_view AnthropicProvider::ModelName() const {
  return config_.model.empty() ? kDefaultAnthropicModel : config_.model;
}

absl::Status AnthropicProvider::Stream(std::span<const Message> messages, const ProviderEventSink& sink) {
  const auto url = provider_endpoint_url(config_.base_url, "messages", kDefaultAnthropicMessagesUrl);

  nlohmann::json body;
  body["model"] = ModelName();
  body["system"] = SerializeAnthropicSystem(messages);
  body["messages"] = SerializeAnthropicMessages(messages);
  body["max_tokens"] = 4096;
  body["stream"] = true;

  if (!tool_descriptions_.empty()) {
    body["tools"] = SerializeAnthropicTools(tool_descriptions_);
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
  bool stream_done = false;

  auto response = provider_http_post_with_retry("Anthropic", [&]() -> absl::StatusOr<network::HttpResponse> {
    stream_parser = AnthropicStreamParser{};
    stream_error.clear();
    stream_done = false;

    return http_.post(url, headers, body_str, [&](std::string_view chunk) {
      if (stream_done) return false;

      auto parsed = stream_parser.Feed(chunk);
      if (!parsed.error.empty() && stream_error.empty()) {
        stream_error = std::move(parsed.error);
      }
      for (const auto& event : parsed.events) {
        sink(event);
      }

      stream_done = parsed.done;
      return !stream_done;
    });
  });

  if (!response.ok()) {
    return absl::UnavailableError("Anthropic HTTP request failed: " + std::string{response.status().message()});
  }

  if (response->status_code != 200) {
    return absl::UnavailableError(provider_http_error_message("Anthropic", *response));
  }

  if (!stream_error.empty()) {
    return absl::UnavailableError(std::move(stream_error));
  }

  return absl::OkStatus();
}

}  // namespace codeharness
