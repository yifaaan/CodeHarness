#include "Context/Compactor.h"
#include "Context/ContextMemory.h"
#include "Context/TokenEstimate.h"

#include <doctest/doctest.h>

#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "Llm/ChatProvider.h"
#include "Llm/Types.h"
#include "absl/status/status.h"

namespace ctx = codeharness::context;
namespace llm = codeharness::llm;

namespace
{

	llm::Message TextMessage(llm::Role role, std::string text)
	{
		llm::Message m;
		m.role = role;
		m.content.push_back(llm::TextPart{std::move(text)});
		return m;
	}

	// Mock provider that returns a fixed summary on the summarization call and
	// an empty text response otherwise. The summarization call is detected by a
	// distinctive system prompt ("You are a concise summarizer.").
	class MockSummarizerProvider : public llm::ChatProvider
	{
	public:
		std::string cannedSummary = "SUMMARY";
		int generateCalls = 0;
		int summarizationCalls = 0;

		std::string Name() const override { return "mock"; }
		std::string ModelName() const override { return "mock-model"; }
		std::optional<llm::ThinkingEffort> ThinkingEffortLevel() const override { return std::nullopt; }

		absl::Status Generate(std::string_view systemPrompt, std::span<const llm::Tool>, std::span<const llm::Message> history,
							  const llm::StreamCallbacks& callbacks, std::stop_token = {}) override
		{
			++generateCalls;
			bool isSummarization = (systemPrompt == "You are a concise summarizer.");
			if (isSummarization)
				++summarizationCalls;

			if (callbacks.onText)
			{
				callbacks.onText(isSummarization ? cannedSummary : std::string{"ok"});
			}
			if (callbacks.onFinish)
			{
				callbacks.onFinish(llm::FinishReason::Completed, llm::TokenUsage{});
			}
			(void)history;
			return absl::OkStatus();
		}
	};

} // namespace

// ---- TokenEstimate ----

TEST_CASE("TokenEstimate: empty string is 0 tokens")
{
	CHECK(ctx::EstimateTokens(std::string_view{""}) == 0);
}

TEST_CASE("TokenEstimate: chars/4 rounding up")
{
	CHECK(ctx::EstimateTokens(std::string_view{"hi"}) == 1);   // 2 chars -> 1
	CHECK(ctx::EstimateTokens(std::string_view{"abcd"}) == 1); // 4 chars -> 1
	CHECK(ctx::EstimateTokens(std::string_view{"abcde"}) == 2); // 5 chars -> 2
	CHECK(ctx::EstimateTokens(std::string_view{"abcdefgh"}) == 2); // 8 chars -> 2
}

TEST_CASE("TokenEstimate: message counts text + overhead")
{
	auto msg = TextMessage(llm::Role::User, "abcdefgh"); // 2 text tokens + per-msg overhead
	auto tokens = ctx::EstimateTokens(msg);
	CHECK(tokens >= 2); // at least the text portion
	CHECK(tokens < 100); // but not absurdly large
}

TEST_CASE("TokenEstimate: tool calls add to the count")
{
	llm::Message withTools = TextMessage(llm::Role::Assistant, "running a tool");
	llm::ToolCall tc;
	tc.id = "call_1";
	tc.name = "Bash";
	tc.arguments = R"({"command":"ls -la"})";
	withTools.toolCalls.push_back(tc);

	auto plain = ctx::EstimateTokens(TextMessage(llm::Role::Assistant, "running a tool"));
	auto withTc = ctx::EstimateTokens(withTools);
	CHECK(withTc > plain); // tool call adds tokens
}

TEST_CASE("TokenEstimate: span overload sums messages")
{
	std::vector<llm::Message> msgs{
		TextMessage(llm::Role::User, "hello"),
		TextMessage(llm::Role::Assistant, "hi there"),
	};
	auto total = ctx::EstimateTokens(msgs);
	CHECK(total > 0);
}

// ---- ContextMemory ----

TEST_CASE("ContextMemory: Append updates token count")
{
	ctx::ContextMemory mem;
	CHECK(mem.Empty());
	CHECK(mem.TokenCount() == 0);

	mem.Append(TextMessage(llm::Role::User, "hello world"));
	CHECK_FALSE(mem.Empty());
	CHECK(mem.Size() == 1);
	CHECK(mem.TokenCount() > 0);
}

TEST_CASE("ContextMemory: ReplaceAll recomputes token count")
{
	ctx::ContextMemory mem;
	mem.Append(TextMessage(llm::Role::User, "x"));
	mem.Append(TextMessage(llm::Role::User, "y"));
	auto two = mem.TokenCount();

	std::vector<llm::Message> one{TextMessage(llm::Role::User, "only")};
	mem.ReplaceAll(std::move(one));
	CHECK(mem.Size() == 1);
	CHECK(mem.TokenCount() != two); // recomputed, not additive
}

TEST_CASE("ContextMemory: Clear resets")
{
	ctx::ContextMemory mem;
	mem.Append(TextMessage(llm::Role::User, "x"));
	CHECK_FALSE(mem.Empty());
	mem.Clear();
	CHECK(mem.Empty());
	CHECK(mem.TokenCount() == 0);
}

// ---- ShouldCompact ----

TEST_CASE("ShouldCompact: disabled when maxContextTokens is 0")
{
	ctx::CompactionConfig cfg; // maxContextTokens = 0
	CHECK_FALSE(ctx::ShouldCompact(1000000, cfg));
}

TEST_CASE("ShouldCompact: false below 75%, true at/above 75%")
{
	ctx::CompactionConfig cfg;
	cfg.maxContextTokens = 1000;
	cfg.compactThreshold = 0.75;

	CHECK_FALSE(ctx::ShouldCompact(100, cfg)); // 10%
	CHECK_FALSE(ctx::ShouldCompact(700, cfg)); // 70%
	CHECK_FALSE(ctx::ShouldCompact(749, cfg)); // 74.9%
	CHECK(ctx::ShouldCompact(750, cfg));	   // exactly 75%
	CHECK(ctx::ShouldCompact(950, cfg));	   // 95%
}

// ---- BuildCompactedHistory ----

TEST_CASE("BuildCompactedHistory: summary + retained tail")
{
	std::vector<llm::Message> history;
	for (int i = 0; i < 20; ++i)
		history.push_back(TextMessage(llm::Role::User, "msg " + std::to_string(i)));

	auto out = ctx::BuildCompactedHistory("the summary", history, 10);
	REQUIRE(out.size() == 11); // 1 summary + 10 retained
	// First message is the summary.
	CHECK(out[0].role == llm::Role::User);
	// Retained tail preserves order: messages 10..19.
	CHECK(std::get<llm::TextPart>(out[1].content[0]).text == "msg 10");
	CHECK(std::get<llm::TextPart>(out[10].content[0]).text == "msg 19");
}

TEST_CASE("BuildCompactedHistory: empty summary yields only the tail")
{
	std::vector<llm::Message> history;
	for (int i = 0; i < 5; ++i)
		history.push_back(TextMessage(llm::Role::User, std::to_string(i)));

	auto out = ctx::BuildCompactedHistory("", history, 3);
	CHECK(out.size() == 3); // no summary message, just the 3 tail messages
}

// ---- Compact (with mock provider) ----

TEST_CASE("Compact: returns nullopt when history is shorter than retainTail")
{
	MockSummarizerProvider provider;
	std::vector<llm::Message> shortHistory{
		TextMessage(llm::Role::User, "a"),
		TextMessage(llm::Role::User, "b"),
	};
	ctx::CompactionConfig cfg;
	cfg.maxContextTokens = 100;
	cfg.retainTail = 10;

	auto r = ctx::Compact(&provider, shortHistory, cfg);
	REQUIRE(r.ok());
	CHECK_FALSE(r->has_value()); // nothing to compact
	CHECK(provider.summarizationCalls == 0);
}

TEST_CASE("Compact: summarizes prefix, keeps tail, drops token count")
{
	MockSummarizerProvider provider;
	provider.cannedSummary = "Compacted summary of the early conversation.";

	std::vector<llm::Message> history;
	for (int i = 0; i < 20; ++i)
		history.push_back(TextMessage(llm::Role::User, "message number " + std::to_string(i)));

	ctx::CompactionConfig cfg;
	cfg.maxContextTokens = 1000;
	cfg.retainTail = 10;

	auto originalTokens = ctx::EstimateTokens(history);

	auto r = ctx::Compact(&provider, history, cfg);
	REQUIRE(r.ok());
	REQUIRE(r->has_value());

	auto& result = **r;
	CHECK(result.summary == "Compacted summary of the early conversation.");
	CHECK(result.removedCount == 10); // 20 - 10 retained
	CHECK(provider.summarizationCalls == 1);

	// The compacted history should be smaller than the original.
	auto compacted = ctx::BuildCompactedHistory(result.summary, history, cfg.retainTail);
	CHECK(ctx::EstimateTokens(compacted) <= originalTokens);
}

TEST_CASE("Compact: null provider is an error")
{
	std::vector<llm::Message> history{TextMessage(llm::Role::User, "x")};
	ctx::CompactionConfig cfg;
	cfg.maxContextTokens = 100;
	auto r = ctx::Compact(nullptr, history, cfg);
	CHECK_FALSE(r.ok());
}
