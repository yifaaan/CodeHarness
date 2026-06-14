#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace codeharness::llm
{

	enum class Role
	{
		User,
		Assistant,
		Tool
	};

	enum class FinishReason
	{
		Completed,
		ToolCalls,
		Truncated,
		Filtered,
		Paused,
		Other,
	};

	enum class ThinkingEffort
	{
		Off,
		Low,
		Medium,
		High,
		XHigh,
		Max
	};

	struct TextPart
	{
		std::string text;
	};

	struct ThinkPart
	{
		std::string think;
		std::optional<std::string> encrypted;
	};

	using ContentPart = std::variant<TextPart, ThinkPart>;

	struct ToolCall
	{
		std::string id;
		std::string name;
		std::string arguments;
	};

	struct Message
	{
		Role role = Role::User;
		std::vector<ContentPart> content;
		std::optional<std::string> toolCallId;
		std::vector<ToolCall> toolCalls;
	};

	struct Tool
	{
		std::string name;
		std::string description;
		nlohmann::json inputSchema;
	};

	struct TokenUsage
	{
		int64_t inputOther = 0;
		int64_t output = 0;
		int64_t inputCacheRead = 0;
		int64_t inputCacheCreation = 0;
	};

	struct ModelCapability
	{
		bool imageIn = false;
		bool videoIn = false;
		bool audioIn = false;
		bool thinking = false;
		bool toolUse = false;
		int64_t maxContextTokens = 0;
	};

	inline constexpr ModelCapability UnknownCapability{};

} // namespace codeharness::llm
