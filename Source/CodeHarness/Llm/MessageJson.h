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
