#include "codeharness/provider/openai_provider.h"

#include <map>
#include <nlohmann/json.hpp>
#include <string>

#include "codeharness/core/log.h"
#include "codeharness/provider/openai_serialize.h"
#include "codeharness/provider/openai_stream_parser.h"
#include "codeharness/provider/provider_http.h"

namespace codeharness {

namespace {

constexpr std::string_view kDefaultOpenAIModel = "gpt-4o";
constexpr std::string_view kDefaultOpenAIResponsesUrl = "https://api.openai.com/v1/responses";

}  // namespace

OpenAIProvider::OpenAIProvider(ProviderConfig config,
                               std::vector<std::pair<std::string, std::string>> tool_descriptions)
    : config_{std::move(config)}, tool_descriptions_{std::move(tool_descriptions)} {}

std::string_view OpenAIProvider::ModelName() const {
  return config_.model.empty() ? kDefaultOpenAIModel : config_.model;
}

absl::Status OpenAIProvider::Stream(std::span<const Message> messages, const ProviderEventSink& sink) {
  auto url = provider_endpoint_url(config_.base_url, "responses", kDefaultOpenAIResponsesUrl);

  nlohmann::json body;
  body["model"] = ModelName();
  body["input"] = SerializeOpenAIInput(messages);
  body["stream"] = true;
  body["max_output_tokens"] = 4096;

  if (!tool_descriptions_.empty()) {
    body["tools"] = SerializeOpenAITools(tool_descriptions_);
  }

  std::map<std::string, std::string> headers;
  headers["Content-Type"] = "application/json";
  headers["Accept"] = "text/event-stream";
  headers["Authorization"] = "Bearer " + config_.api_key;

  auto body_str = body.dump();
  spdlog::debug("OpenAI request: POST {} ({} bytes)", url, body_str.size());

  OpenAIStreamParser stream_parser;
  std::string stream_error;
  bool stream_done = false;

  auto response = provider_http_post_with_retry("OpenAI", [&]() -> absl::StatusOr<network::HttpResponse> {
    stream_parser = OpenAIStreamParser{};
    stream_error.clear();
    stream_done = false;

    return http_.post(url, headers, body_str, [&](std::string_view chunk) {
      if (stream_done) return false;

      auto parsed = stream_parser.Feed(chunk);
      if (!parsed.error.empty() && stream_error.empty()) {
        stream_error = parsed.error;
      }
      for (const auto& evt : parsed.events) {
        sink(evt);
      }

      stream_done = parsed.done;
      return !stream_done;
    });
  });

  if (!response.ok()) {
    return absl::UnavailableError("OpenAI HTTP request failed: " + std::string{response.status().message()});
  }

  if (response->status_code != 200) {
    return absl::UnavailableError(provider_http_error_message("OpenAI", *response));
  }

  if (!stream_error.empty()) {
    return absl::UnavailableError(std::move(stream_error));
  }

  return absl::OkStatus();
}

}  // namespace codeharness
