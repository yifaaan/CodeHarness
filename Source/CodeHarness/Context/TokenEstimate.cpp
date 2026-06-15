#include "Context/TokenEstimate.h"

#include <string>
#include <utility>

#include "Llm/MessageJson.h"
#include "Llm/Types.h"
#include "nlohmann/json.hpp"

namespace codeharness::context
{

	namespace
	{

		// ~4 chars per token is the conventional English/code heuristic.
		constexpr int64_t CharsPerToken = 4;
		// Small fixed overhead per message (role tags, separators) and per
		// tool call (the JSON envelope). Both bias the estimate upward.
		constexpr int64_t PerMessageOverhead = 4;
		constexpr int64_t PerToolCallOverhead = 8;

		int64_t EstimateToolCallTokens(const llm::ToolCall& tc)
		{
			// id + name + arguments, roughly serialized length / chars-per-token.
			auto len = tc.id.size() + tc.name.size() + tc.arguments.size();
			return static_cast<int64_t>(len) / CharsPerToken + PerToolCallOverhead;
		}

	} // namespace

	int64_t EstimateTokens(std::string_view text)
	{
		if (text.empty())
			return 0;
		// (size + CharsPerToken - 1) / CharsPerToken rounds up so a 5-char
		// word counts as 2 tokens, not 1.
		return (static_cast<int64_t>(text.size()) + CharsPerToken - 1) / CharsPerToken;
	}

	int64_t EstimateTokens(const llm::Message& msg)
	{
		int64_t total = PerMessageOverhead + EstimateTokens(llm::ConcatTextParts(msg.content));
		for (const auto& tc : msg.toolCalls)
		{
			total += EstimateToolCallTokens(tc);
		}
		if (msg.toolCallId)
		{
			total += EstimateTokens(*msg.toolCallId);
		}
		return total;
	}

	int64_t EstimateTokens(std::span<const llm::Message> msgs)
	{
		int64_t total = 0;
		for (const auto& msg : msgs)
		{
			total += EstimateTokens(msg);
		}
		return total;
	}

} // namespace codeharness::context
