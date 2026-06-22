#include "MessageJson.h"

#include <absl/status/status.h>
#include <fmt/format.h>

#include <string>

namespace codeharness::llm
{

	namespace
	{

		// (ConcatTextParts is now defined as a public function below; the
		// anonymous namespace here is reserved for helpers that are truly
		// file-local.)

	} // namespace

	std::string ConcatTextParts(const std::vector<ContentPart>& parts)
	{
		std::string result;
		for (const auto& part : parts)
		{
			if (auto* text = std::get_if<TextPart>(&part))
			{
				if (!result.empty())
					result += '\n';
				result += text->text;
			}
			else if (auto* image = std::get_if<ImagePart>(&part))
			{
				if (!result.empty())
					result += '\n';
				result += fmt::format("[image: {}]", image->url);
			}
			else if (auto* video = std::get_if<VideoPart>(&part))
			{
				if (!result.empty())
					result += '\n';
				result += fmt::format("[video: {}]", video->url);
			}
		}
		return result;
	}

	namespace
	{

		nlohmann::json MessageContentToJson(const std::vector<ContentPart>& parts)
		{
			bool rich = false;
			for (const auto& part : parts)
			{
				rich = rich || std::holds_alternative<ImagePart>(part) || std::holds_alternative<VideoPart>(part);
			}
			if (!rich)
			{
				return ConcatTextParts(parts);
			}

			auto content = nlohmann::json::array();
			for (const auto& part : parts)
			{
				if (auto* text = std::get_if<TextPart>(&part))
				{
					content.push_back({{"type", "text"}, {"text", text->text}});
				}
				else if (auto* image = std::get_if<ImagePart>(&part))
				{
					nlohmann::json obj = {{"type", "image_url"}, {"image_url", {{"url", image->url}}}};
					if (!image->detail.empty())
					{
						obj["image_url"]["detail"] = image->detail;
					}
					content.push_back(std::move(obj));
				}
				else if (auto* video = std::get_if<VideoPart>(&part))
				{
					content.push_back({{"type", "text"}, {"text", fmt::format("[video: {}]", video->url)}});
				}
			}
			return content;
		}

	} // namespace

	nlohmann::json MessagesToJson(std::string_view systemPrompt, std::span<const Message> messages)
	{
		auto arr = nlohmann::json::array();

		if (!systemPrompt.empty())
		{
			arr.push_back({{"role", "system"}, {"content", std::string(systemPrompt)}});
		}

		for (const auto& msg : messages)
		{
			nlohmann::json obj;
			obj["role"] = msg.role == Role::User ? "user" : msg.role == Role::Assistant ? "assistant"
																						: "tool";

			if (msg.role == Role::Tool && msg.toolCallId)
			{
				obj["tool_call_id"] = *msg.toolCallId;
			}

			obj["content"] = MessageContentToJson(msg.content);

			if (msg.role == Role::Assistant && !msg.toolCalls.empty())
			{
				auto calls = nlohmann::json::array();
				for (const auto& tc : msg.toolCalls)
				{
					calls.push_back(
						{{"id", tc.id}, {"type", "function"}, {"function", {{"name", tc.name}, {"arguments", tc.arguments}}}});
				}
				obj["tool_calls"] = calls;
			}

			arr.push_back(std::move(obj));
		}

		return arr;
	}

	nlohmann::json ToolsToJson(std::span<const Tool> tools)
	{
		auto arr = nlohmann::json::array();
		for (const auto& tool : tools)
		{
			auto func = nlohmann::json{
				{"name", tool.name},
				{"description", tool.description},
				{"parameters", tool.inputSchema.is_null() ? nlohmann::json::object() : tool.inputSchema},
			};
			arr.push_back({{"type", "function"}, {"function", std::move(func)}});
		}
		return arr;
	}

	absl::StatusOr<StreamChunk> ParseStreamChunk(const std::string& json_str)
	{
		nlohmann::json j;
		try
		{
			j = nlohmann::json::parse(json_str);
		}
		catch (const nlohmann::json::parse_error& e)
		{
			return absl::InternalError(fmt::format("failed to parse SSE chunk: {}", e.what()));
		}

		StreamChunk chunk;

		if (j.contains("usage") && j["usage"].is_object())
		{
			const auto& u = j["usage"];
			TokenUsage usage;
			usage.output = u.value("completion_tokens", 0);
			int64_t prompt = u.value("prompt_tokens", 0);
			int64_t cached = 0;
			if (u.contains("prompt_tokens_details") && u["prompt_tokens_details"].is_object())
			{
				cached = u["prompt_tokens_details"].value("cached_tokens", 0);
			}
			usage.inputOther = prompt - cached;
			usage.inputCacheRead = cached;
			chunk.usage = usage;
		}

		if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty())
		{
			return chunk;
		}

		const auto& choice = j["choices"][0];

		if (choice.contains("finish_reason") && !choice["finish_reason"].is_null())
		{
			chunk.finishReason = choice["finish_reason"].get<std::string>();
		}

		if (!choice.contains("delta") || !choice["delta"].is_object())
		{
			return chunk;
		}

		const auto& delta = choice["delta"];

		if (delta.contains("content") && !delta["content"].is_null())
		{
			chunk.content = delta["content"].get<std::string>();
		}

		// Reasoning/thinking text. Different providers spell it differently
		// (`reasoning`, `reasoning_content`); accept either when present and non-null.
		for (const char* key : {"reasoning", "reasoning_content"})
		{
			if (delta.contains(key) && !delta[key].is_null() && delta[key].is_string())
			{
				chunk.reasoning = delta[key].get<std::string>();
				break;
			}
		}

		if (delta.contains("tool_calls") && delta["tool_calls"].is_array() && !delta["tool_calls"].empty())
		{
			const auto& tc = delta["tool_calls"][0];
			if (tc.contains("index"))
				chunk.toolCallIndex = tc["index"].get<int>();
			if (tc.contains("id") && !tc["id"].is_null())
				chunk.toolCallId = tc["id"].get<std::string>();
			if (tc.contains("function") && tc["function"].is_object())
			{
				const auto& fn = tc["function"];
				if (fn.contains("name") && !fn["name"].is_null())
					chunk.toolCallName = fn["name"].get<std::string>();
				if (fn.contains("arguments") && !fn["arguments"].is_null())
					chunk.toolCallArgs = fn["arguments"].get<std::string>();
			}
		}

		return chunk;
	}

	FinishReason MapFinishReason(std::string_view reason)
	{
		if (reason == "stop")
			return FinishReason::Completed;
		if (reason == "tool_calls")
			return FinishReason::ToolCalls;
		if (reason == "length")
			return FinishReason::Truncated;
		if (reason == "content_filter")
			return FinishReason::Filtered;
		return FinishReason::Other;
	}

} // namespace codeharness::llm
