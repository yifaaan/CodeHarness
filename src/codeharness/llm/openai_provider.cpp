#include "openai_provider.h"

#include <absl/status/status.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <nlohmann/json.hpp>
#include <utility>

#include "message_json.h"
#include "sse_parser.h"

namespace codeharness::llm
{

namespace
{

absl::Status HttpStatusToAbseil(int status, std::string_view body)
{
	if (status == 401)
		return absl::UnauthenticatedError("invalid API key");
	if (status == 403)
		return absl::PermissionDeniedError("access forbidden");
	if (status == 429)
		return absl::ResourceExhaustedError("rate limited");
	if (status == 400)
	{
		if (body.find("context_length_exceeded") != std::string::npos ||
			body.find("maximum context length") != std::string::npos)
		{
			return absl::InvalidArgumentError("context overflow");
		}
		return absl::InvalidArgumentError(fmt::format("bad request: {}", body));
	}
	return absl::InternalError(fmt::format("HTTP {}: {}", status, body));
}

} // namespace

OpenAiProvider::OpenAiProvider(OpenAiConfig _config, HttpClient* _http) : config(std::move(_config)), http(_http)
{
	if (config.apiKey.empty())
	{
		if (const char* env = std::getenv("OPENAI_API_KEY"))
		{
			config.apiKey = env;
		}
	}
}

std::string OpenAiProvider::Name() const
{
	return "openai";
}

std::string OpenAiProvider::ModelName() const
{
	return config.model;
}

std::optional<ThinkingEffort> OpenAiProvider::ThinkingEffortLevel() const
{
	return config.thinking;
}

absl::Status OpenAiProvider::Generate(std::string_view systemPrompt, std::span<const Tool> tools, std::span<const Message> history, const StreamCallbacks& callbacks, std::stop_token stopToken)
{
	if (config.apiKey.empty())
	{
		return absl::FailedPreconditionError("no API key configured (set OPENAI_API_KEY or pass to config)");
	}

	auto body = nlohmann::json::object();
	body["model"] = config.model;
	body["stream"] = true;
	body["stream_options"] = nlohmann::json::object({{"include_usage", true}});
	body["messages"] = MessagesToJson(systemPrompt, history);

	if (!tools.empty())
	{
		body["tools"] = ToolsToJson(tools);
	}

	if (config.maxCompletionTokens > 0)
	{
		body["max_completion_tokens"] = config.maxCompletionTokens;
	}

	HttpRequest req;
	req.host = config.host;
	req.port = "443";
	req.path = config.path;
	req.method = "POST";
	req.useTls = true;
	req.headers = {{"Authorization", "Bearer " + config.apiKey},
				   {"Content-Type", "application/json"},
				   {"Accept", "text/event-stream"}};
	req.body = body.dump();

	SseParser parser;
	FinishReason finish = FinishReason::Other;
	TokenUsage usage{};
	bool gotFinish = false;

	auto onChunk = [&](std::string_view data) -> bool {
		if (stopToken.stop_requested())
			return false;

		parser.Feed(data);
		while (auto event = parser.NextEvent())
		{
			auto chunk = ParseStreamChunk(*event);
			if (!chunk.ok())
			{
				spdlog::warn("failed to parse SSE chunk: {}", chunk.status().message());
				continue;
			}

			if (chunk->content && callbacks.onText)
			{
				callbacks.onText(*chunk->content);
			}

			if (chunk->toolCallIndex)
			{
				if (chunk->toolCallId && chunk->toolCallName && callbacks.onToolCallStart)
				{
					callbacks.onToolCallStart(*chunk->toolCallIndex, *chunk->toolCallId, *chunk->toolCallName);
				}
				if (chunk->toolCallArgs && callbacks.onToolCallDelta)
				{
					callbacks.onToolCallDelta(*chunk->toolCallIndex, *chunk->toolCallArgs);
				}
			}

			if (chunk->finishReason)
			{
				finish = MapFinishReason(*chunk->finishReason);
				gotFinish = true;
			}

			if (chunk->usage)
			{
				usage = *chunk->usage;
			}
		}
		return true;
	};

	auto response = http->StreamRequest(req, onChunk, stopToken);
	if (!response.ok())
	{
		return response.status();
	}

	if (response->status != 200)
	{
		return HttpStatusToAbseil(response->status, response->body);
	}

	if (!gotFinish)
	{
		return absl::InternalError("stream ended without finish_reason");
	}

	if (callbacks.onFinish)
	{
		callbacks.onFinish(finish, usage);
	}

	return absl::OkStatus();
}

} // namespace codeharness::llm
