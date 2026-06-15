#pragma once

#include <vector>

#include "LoopTypes.h"

namespace codeharness::engine
{

	struct ScheduledToolResult
	{
		llm::ToolCall call;
		nlohmann::json args;
		ToolResult result;
	};

	std::vector<ScheduledToolResult> RunToolCallBatch(
		const std::vector<llm::ToolCall>& toolCalls,
		const ToolContext& ctx,
		const TurnInput& input);

} // namespace codeharness::engine
