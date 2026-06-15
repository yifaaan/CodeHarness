#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "Llm/Types.h"

namespace codeharness::context
{

	// Rough token estimate for compaction triggering. Uses the standard
	// chars/4 heuristic (OpenAI-ish) plus small per-message and per-tool-call
	// overhead. Deliberately conservative — it tends to over-count slightly,
	// which fires the compaction trigger a little early. That is safe: a real
	// tokenizer would only ever be more precise. Swap the implementation here
	// to upgrade to a provider tokenizer without touching callers.
	//
	// This is NOT used for billing or for reporting prompt-token usage to the
	// model; it is only used to decide when to compact.

	int64_t EstimateTokens(std::string_view text);
	int64_t EstimateTokens(const llm::Message& msg);
	int64_t EstimateTokens(std::span<const llm::Message> msgs);

} // namespace codeharness::context
