#include "Context/Compactor.h"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Context/TokenEstimate.h"
#include "Llm/ChatProvider.h"
#include "Llm/MessageJson.h"
#include "Llm/Types.h"
#include "absl/status/status.h"
#include "fmt/format.h"

namespace codeharness::context
{

	namespace
	{

		constexpr std::string_view kSummaryPrefix = "# Conversation summary\n\n";

		// Prompt the model to produce a concise summary of the conversation so
		// far. Kept deliberately generic; tuned for "preserve the facts the next
		// turn needs" rather than literary compression.
		std::string BuildSummarizationPrompt(std::span<const llm::Message> prefix)
		{
			std::string transcript;
			for (const auto& msg : prefix)
			{
				std::string_view role = msg.role == llm::Role::User ? "User" : msg.role == llm::Role::Assistant ? "Assistant"
																												 : "Tool";
				transcript += fmt::format("[{}]: {}\n", role, llm::ConcatTextParts(msg.content));
			}
			return fmt::format(
				"Summarize the conversation below so a fresh assistant can continue the task with minimal loss of "
				"important context. Preserve: the user's goal, key decisions, file paths or commands mentioned, "
				"open questions, and any partial results. Keep it concise and factual — no preamble.\n\n"
				"Conversation:\n{}\n\nSummary:",
				transcript);
		}

	} // namespace

	bool ShouldCompact(int64_t usedTokens, const CompactionConfig& cfg)
	{
		if (cfg.maxContextTokens <= 0)
		{
			return false;
		}
		int64_t threshold = static_cast<int64_t>(static_cast<double>(cfg.maxContextTokens) * cfg.compactThreshold);
		return usedTokens >= threshold;
	}

	std::vector<llm::Message> BuildCompactedHistory(
		std::string_view summary,
		std::span<const llm::Message> history,
		int retainTail)
	{
		std::vector<llm::Message> out;

		if (!summary.empty())
		{
			llm::Message summaryMsg;
			summaryMsg.role = llm::Role::User;
			summaryMsg.content.push_back(llm::TextPart{fmt::format("{}{}", kSummaryPrefix, summary)});
			out.push_back(std::move(summaryMsg));
		}

		// Keep the last `retainTail` messages verbatim (preserves tool-call
		// structure the model may still need to reference).
		int total = static_cast<int>(history.size());
		int start = std::max(0, total - retainTail);
		for (int i = start; i < total; ++i)
		{
			out.push_back(history[i]);
		}
		return out;
	}

	absl::StatusOr<std::optional<CompactionResult>> Compact(
		llm::ChatProvider* provider,
		std::span<const llm::Message> history,
		const CompactionConfig& cfg,
		std::stop_token stopToken)
	{
		if (provider == nullptr)
		{
			return absl::InvalidArgumentError("Compact requires a provider");
		}
		if (cfg.retainTail < 0)
		{
			return absl::InvalidArgumentError("retainTail must be non-negative");
		}

		// Nothing to compact if the history is shorter than (or equal to) the
		// tail we'd retain — there'd be no prefix to summarize.
		if (static_cast<int>(history.size()) <= cfg.retainTail)
		{
			return std::nullopt;
		}

		int prefixEnd = static_cast<int>(history.size()) - cfg.retainTail;
		std::span<const llm::Message> prefix = history.subspan(0, prefixEnd);

		std::string prompt = BuildSummarizationPrompt(prefix);

		// One-shot Generate: no tools, no streaming. The summarization prompt
		// is passed as a single user message in the history span.
		llm::Message promptMsg;
		promptMsg.role = llm::Role::User;
		promptMsg.content.push_back(llm::TextPart{std::move(prompt)});
		std::vector<llm::Message> summarizationHistory{std::move(promptMsg)};

		std::string summary;
		llm::StreamCallbacks callbacks{
			.onText = [&](std::string_view text) { summary += text; },
			.onThink = {},
			.onToolCallStart = {},
			.onToolCallDelta = {},
			.onFinish = {},
		};

		auto status = provider->Generate(
			"You are a concise summarizer.",
			{},
			std::span<const llm::Message>(summarizationHistory),
			callbacks,
			stopToken);
		if (!status.ok())
		{
			return status;
		}

		if (summary.empty())
		{
			// Treat an empty summary as "nothing to do" rather than producing a
			// contentless summary message.
			return std::nullopt;
		}

		auto compacted = BuildCompactedHistory(summary, history, cfg.retainTail);
		CompactionResult result;
		result.summary = std::move(summary);
		result.newTokenCount = EstimateTokens(compacted);
		result.removedCount = prefixEnd;
		return result;
	}

} // namespace codeharness::context
