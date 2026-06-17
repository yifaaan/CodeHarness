#include "OpenAiProvider.h"

#include <absl/status/status.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <exception>
#include <nlohmann/json.hpp>
#include <typeinfo>
#include <utility>

#include "MessageJson.h"
#include "SseParser.h"

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
		spdlog::debug("openai: Generate start model={} host={} port={} tls={} path={} system_len={} history={} tools={}",
					  config.model, config.host, config.port, config.useTls, config.path, systemPrompt.size(), history.size(), tools.size());
		if (config.apiKey.empty())
		{
			return absl::FailedPreconditionError("no API key configured (set OPENAI_API_KEY or pass to config)");
		}

		HttpRequest req;
		try
		{
			auto body = nlohmann::json::object();
			body["model"] = config.model;
			body["stream"] = true;
			body["stream_options"] = nlohmann::json::object({{"include_usage", true}});
			spdlog::debug("openai: building messages json history={}", history.size());
			body["messages"] = MessagesToJson(systemPrompt, history);

			if (!tools.empty())
			{
				spdlog::debug("openai: building tools json count={}", tools.size());
				body["tools"] = ToolsToJson(tools);
			}

			if (config.maxCompletionTokens > 0)
			{
				body["max_completion_tokens"] = config.maxCompletionTokens;
			}

			req.body = body.dump();
			spdlog::debug("openai: request body bytes={}", req.body.size());
		}
		catch (const std::bad_alloc&)
		{
			spdlog::error("openai: bad_alloc while building request history={} tools={}", history.size(), tools.size());
			return absl::ResourceExhaustedError("bad allocation while building OpenAI request");
		}
		catch (const std::exception& e)
		{
			spdlog::error("openai: exception while building request type={} what={}", typeid(e).name(), e.what());
			return absl::InternalError(fmt::format("failed to build OpenAI request: {}", e.what()));
		}

		req.host = config.host;
		req.port = config.port;
		req.path = config.path;
		req.method = "POST";
		req.useTls = config.useTls;
		req.headers = {{"Authorization", "Bearer " + config.apiKey},
					   {"Content-Type", "application/json"},
					   {"Accept", "text/event-stream"}};

		SseParser parser;
		FinishReason finish = FinishReason::Other;
		TokenUsage usage{};
		bool gotFinish = false;

		auto onChunk = [&](std::string_view data) -> bool {
			if (stopToken.stop_requested())
				return false;

			spdlog::trace("openai: stream chunk bytes={}", data.size());
			parser.Feed(data);
			while (auto event = parser.NextEvent())
			{
				spdlog::trace("openai: sse event bytes={}", event->size());
				auto chunk = ParseStreamChunk(*event);
				if (!chunk.ok())
				{
					spdlog::warn("failed to parse SSE chunk: {}", chunk.status().message());
					continue;
				}

				if (chunk->content && callbacks.onText)
				{
					spdlog::trace("openai: text delta bytes={}", chunk->content->size());
					callbacks.onText(*chunk->content);
				}

				if (chunk->toolCallIndex)
				{
					spdlog::debug("openai: tool delta index={} has_id={} has_name={} args_bytes={}",
								  *chunk->toolCallIndex,
								  chunk->toolCallId.has_value(),
								  chunk->toolCallName.has_value(),
								  chunk->toolCallArgs ? chunk->toolCallArgs->size() : 0);
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

		spdlog::debug("openai: StreamRequest start host={} port={} tls={} path={}", req.host, req.port, req.useTls, req.path);
		auto response = http->StreamRequest(req, onChunk, stopToken);
		if (!response.ok())
		{
			spdlog::debug("openai: StreamRequest status error={}", response.status().message());
			return response.status();
		}
		spdlog::debug("openai: StreamRequest done http_status={} finish_seen={}", response->status, gotFinish);

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
