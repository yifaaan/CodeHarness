#pragma once

#include <absl/status/statusor.h>

#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "types.h"

namespace codeharness::llm
{

	nlohmann::json MessagesToJson(std::string_view systemPrompt, std::span<const Message> messages);

	nlohmann::json ToolsToJson(std::span<const Tool> tools);

	// Flatten a message's content parts into a single string, joining TextParts
	// with newlines. Non-text parts (e.g. ThinkPart) are skipped. Exported so the
	// Context module can estimate token counts from message text.
	std::string ConcatTextParts(const std::vector<ContentPart>& parts);

	struct StreamChunk
	{
		std::optional<std::string> content;
		std::optional<int> toolCallIndex;
		std::optional<std::string> toolCallId;
		std::optional<std::string> toolCallName;
		std::optional<std::string> toolCallArgs;
		std::optional<std::string> finishReason;
		std::optional<TokenUsage> usage;
	};

	absl::StatusOr<StreamChunk> ParseStreamChunk(const std::string& jsonStr);

	FinishReason MapFinishReason(std::string_view reason);

} // namespace codeharness::llm
