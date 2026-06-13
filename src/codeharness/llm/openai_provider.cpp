#include "openai_provider.h"

#include <absl/status/status.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <nlohmann/json.hpp>
#include <utility>

#include "message_json.h"
#include "sse_parser.h"

namespace codeharness::llm {

namespace {

absl::Status HttpStatusToAbseil(int status, std::string_view body) {
  if (status == 401) return absl::UnauthenticatedError("invalid API key");
  if (status == 403) return absl::PermissionDeniedError("access forbidden");
  if (status == 429) return absl::ResourceExhaustedError("rate limited");
  if (status == 400) {
    if (body.find("context_length_exceeded") != std::string::npos ||
        body.find("maximum context length") != std::string::npos) {
      return absl::InvalidArgumentError("context overflow");
    }
    return absl::InvalidArgumentError(fmt::format("bad request: {}", body));
  }
  return absl::InternalError(fmt::format("HTTP {}: {}", status, body));
}

}  // namespace

OpenAiProvider::OpenAiProvider(OpenAiConfig config, HttpClient* http) : config_(std::move(config)), http_(http) {
  if (config_.api_key.empty()) {
    if (const char* env = std::getenv("OPENAI_API_KEY")) {
      config_.api_key = env;
    }
  }
}

std::string OpenAiProvider::Name() const { return "openai"; }

std::string OpenAiProvider::ModelName() const { return config_.model; }

std::optional<ThinkingEffort> OpenAiProvider::ThinkingEffortLevel() const { return config_.thinking; }

absl::Status OpenAiProvider::Generate(std::string_view system_prompt, std::span<const Tool> tools,
                                      std::span<const Message> history, const StreamCallbacks& callbacks,
                                      std::stop_token stop_token) {
  if (config_.api_key.empty()) {
    return absl::FailedPreconditionError("no API key configured (set OPENAI_API_KEY or pass to config)");
  }

  auto body = nlohmann::json::object();
  body["model"] = config_.model;
  body["stream"] = true;
  body["stream_options"] = nlohmann::json::object({{"include_usage", true}});
  body["messages"] = MessagesToJson(system_prompt, history);

  if (!tools.empty()) {
    body["tools"] = ToolsToJson(tools);
  }

  if (config_.max_completion_tokens > 0) {
    body["max_completion_tokens"] = config_.max_completion_tokens;
  }

  HttpRequest req;
  req.host = config_.host;
  req.port = "443";
  req.path = config_.path;
  req.method = "POST";
  req.use_tls = true;
  req.headers = {{"Authorization", "Bearer " + config_.api_key},
                 {"Content-Type", "application/json"},
                 {"Accept", "text/event-stream"}};
  req.body = body.dump();

  SseParser parser;
  FinishReason finish = FinishReason::kOther;
  TokenUsage usage{};
  bool got_finish = false;

  auto on_chunk = [&](std::string_view data) -> bool {
    if (stop_token.stop_requested()) return false;

    parser.Feed(data);
    while (auto event = parser.NextEvent()) {
      auto chunk = ParseStreamChunk(*event);
      if (!chunk.ok()) {
        spdlog::warn("failed to parse SSE chunk: {}", chunk.status().message());
        continue;
      }

      if (chunk->content && callbacks.on_text) {
        callbacks.on_text(*chunk->content);
      }

      if (chunk->tool_call_index) {
        if (chunk->tool_call_id && chunk->tool_call_name && callbacks.on_tool_call_start) {
          callbacks.on_tool_call_start(*chunk->tool_call_index, *chunk->tool_call_id, *chunk->tool_call_name);
        }
        if (chunk->tool_call_args && callbacks.on_tool_call_delta) {
          callbacks.on_tool_call_delta(*chunk->tool_call_index, *chunk->tool_call_args);
        }
      }

      if (chunk->finish_reason) {
        finish = MapFinishReason(*chunk->finish_reason);
        got_finish = true;
      }

      if (chunk->usage) {
        usage = *chunk->usage;
      }
    }
    return true;
  };

  auto response = http_->StreamRequest(req, on_chunk, stop_token);
  if (!response.ok()) {
    return response.status();
  }

  if (response->status != 200) {
    return HttpStatusToAbseil(response->status, response->body);
  }

  if (!got_finish) {
    return absl::InternalError("stream ended without finish_reason");
  }

  if (callbacks.on_finish) {
    callbacks.on_finish(finish, usage);
  }

  return absl::OkStatus();
}

}  // namespace codeharness::llm
