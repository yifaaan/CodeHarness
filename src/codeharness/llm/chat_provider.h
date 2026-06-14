#pragma once

#include <absl/status/status.h>

#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>

#include "types.h"

namespace codeharness::llm
{

struct StreamCallbacks
{
	std::function<void(std::string_view)> onText;
	std::function<void(std::string_view)> onThink;
	std::function<void(int index, std::string_view id, std::string_view name)> onToolCallStart;
	std::function<void(int index, std::string_view argsChunk)> onToolCallDelta;
	std::function<void(FinishReason, const TokenUsage&)> onFinish;
};

class ChatProvider
{
public:
	virtual ~ChatProvider() = default;

	virtual std::string Name() const = 0;
	virtual std::string ModelName() const = 0;
	virtual std::optional<ThinkingEffort> ThinkingEffortLevel() const = 0;

	virtual absl::Status Generate(std::string_view systemPrompt, std::span<const Tool> tools, std::span<const Message> history, const StreamCallbacks& callbacks, std::stop_token stopToken = {}) = 0;
};

} // namespace codeharness::llm
