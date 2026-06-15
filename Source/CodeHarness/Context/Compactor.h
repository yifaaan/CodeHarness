#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include "Llm/Types.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness::llm
{
	class ChatProvider;
}

namespace codeharness::context
{

	// Tuning knobs for compaction. Defaults match the reference design
	// (context-compaction.md): compact at 75% of the window, keep the last
	// 10 messages verbatim.
	struct CompactionConfig
	{
		int64_t maxContextTokens = 0; // 0 disables compaction (set from GetCapability)
		double compactThreshold = 0.75;
		int retainTail = 10;
	};

	// Outcome of a single compaction pass.
	struct CompactionResult
	{
		std::string summary;
		int64_t newTokenCount = 0; // estimated tokens of the compacted history
		int removedCount = 0;		 // number of messages summarized away
	};

	// True if `usedTokens` crosses the configured threshold and compaction
	// should run. Always false when maxContextTokens == 0 (disabled).
	bool ShouldCompact(int64_t usedTokens, const CompactionConfig& cfg);

	// Run a compaction pass: summarize the prefix (all but the last
	// `cfg.retainTail` messages) via a second Generate call to `provider`,
	// then return the summary plus the estimated token count of the resulting
	// (summary + retained tail) history.
	//
	// Returns nullopt (not an error) when there is nothing meaningful to
	// compact — e.g. fewer than retainTail+1 messages, or an empty history.
	// Returns a non-OK status only if the summarization Generate call fails.
	absl::StatusOr<std::optional<CompactionResult>> Compact(
		llm::ChatProvider* provider,
		std::span<const llm::Message> history,
		const CompactionConfig& cfg,
		std::stop_token stopToken = {});

	// Build the compacted message list: one summary message followed by the
	// retained tail. Public so tests can exercise the layout without a provider.
	std::vector<llm::Message> BuildCompactedHistory(
		std::string_view summary,
		std::span<const llm::Message> history,
		int retainTail);

} // namespace codeharness::context
