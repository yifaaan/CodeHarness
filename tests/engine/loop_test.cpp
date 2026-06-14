#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "absl/status/status.h"
#include "fmt/format.h"

#include "engine/loop.h"
#include "engine/loop_types.h"
#include "engine/tool.h"
#include "llm/chat_provider.h"
#include "llm/types.h"

namespace eng = codeharness::engine;
namespace llm = codeharness::llm;
using json = nlohmann::json;

namespace
{

struct CannedResponse
{
	std::string text;
	struct ToolCallSpec
	{
		int index;
		std::string id;
		std::string name;
		std::string arguments;
	};
	std::vector<ToolCallSpec> toolCalls;
	llm::FinishReason finish = llm::FinishReason::Completed;
	llm::TokenUsage usage{};
};

class MockChatProvider : public llm::ChatProvider
{
public:
	std::vector<CannedResponse> responses;
	size_t callCount = 0;

	std::string Name() const override
	{
		return "mock";
	}
	std::string ModelName() const override
	{
		return "mock-model";
	}
	std::optional<llm::ThinkingEffort> ThinkingEffortLevel() const override
	{
		return std::nullopt;
	}

	absl::Status Generate(std::string_view systemPrompt, std::span<const llm::Tool> tools, std::span<const llm::Message> history, const llm::StreamCallbacks& callbacks, std::stop_token stopToken = {}) override
	{
		size_t idx = callCount++;
		if (idx >= responses.size())
		{
			return absl::InternalError("no more canned responses");
		}

		const auto& resp = responses[idx];

		if (!resp.text.empty() && callbacks.onText)
		{
			callbacks.onText(resp.text);
		}

		for (const auto& tc : resp.toolCalls)
		{
			if (callbacks.onToolCallStart)
			{
				callbacks.onToolCallStart(tc.index, tc.id, tc.name);
			}
			if (!tc.arguments.empty() && callbacks.onToolCallDelta)
			{
				callbacks.onToolCallDelta(tc.index, tc.arguments);
			}
		}

		if (callbacks.onFinish)
		{
			callbacks.onFinish(resp.finish, resp.usage);
		}

		return absl::OkStatus();
	}
};

class MockTool : public eng::ExecutableTool
{
public:
	std::string toolName;
	std::string toolDesc;
	json toolParams = json::object();
	std::function<eng::ToolResult(const json&)> handler;

	std::string Name() const override
	{
		return toolName;
	}
	std::string Description() const override
	{
		return toolDesc;
	}
	json Parameters() const override
	{
		return toolParams;
	}

	absl::StatusOr<eng::ToolExecution> ResolveExecution(const json& args) override
	{
		return eng::ToolExecution{.description = fmt::format("execute {}", toolName)};
	}

	absl::StatusOr<eng::ToolResult> Execute(const json& args, const eng::ToolContext& ctx) override
	{
		if (handler)
			return handler(args);
		return eng::ToolResult{.content = "ok", .isError = false};
	}
};

struct EventLog
{
	std::vector<eng::LoopEvent> events;

	int CountStepStarted() const
	{
		return std::count_if(events.begin(), events.end(), [](const auto& e) {
			return std::holds_alternative<eng::StepStartedEvent>(e);
		});
	}

	int CountAssistantDeltas() const
	{
		return std::count_if(events.begin(), events.end(), [](const auto& e) {
			return std::holds_alternative<eng::AssistantDeltaEvent>(e);
		});
	}

	int CountToolResults() const
	{
		return std::count_if(events.begin(), events.end(), [](const auto& e) {
			return std::holds_alternative<eng::ToolResultEvent>(e);
		});
	}

	std::optional<eng::ToolResultEvent> LastToolResult() const
	{
		for (auto it = events.rbegin(); it != events.rend(); ++it)
		{
			if (auto* tr = std::get_if<eng::ToolResultEvent>(&*it))
				return *tr;
		}
		return std::nullopt;
	}

	std::string AllText() const
	{
		std::string result;
		for (const auto& e : events)
		{
			if (auto* d = std::get_if<eng::AssistantDeltaEvent>(&e))
			{
				result += d->text;
			}
		}
		return result;
	}

	eng::EventDispatcher MakeDispatcher()
	{
		return [this](const eng::LoopEvent& e) { events.push_back(e); };
	}
};

CannedResponse::ToolCallSpec TC(int idx, std::string id, std::string name, std::string args = "{}")
{
	return {idx, std::move(id), std::move(name), std::move(args)};
}

} // namespace

TEST_CASE("Loop: text-only response completes in 1 step")
{
	MockChatProvider provider;
	provider.responses = {
		{.text = "Hello!", .finish = llm::FinishReason::Completed},
	};

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.systemPrompt = "be helpful",
		.dispatchEvent = log.MakeDispatcher(),
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Completed);
	CHECK(result.stepsExecuted == 1);
	CHECK(log.AllText() == "Hello!");
	CHECK(log.CountStepStarted() == 1);
}

TEST_CASE("Loop: single tool call then text completes in 2 steps")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "call_1", "echo", R"({"msg":"hi"})")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "Done!", .finish = llm::FinishReason::Completed},
	};

	MockTool echo;
	echo.toolName = "echo";
	echo.handler = [](const json& args) -> eng::ToolResult {
		return {.content = args.value("msg", "no msg")};
	};

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&echo},
		.dispatchEvent = log.MakeDispatcher(),
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Completed);
	CHECK(result.stepsExecuted == 2);
	CHECK(log.AllText() == "Done!");
	CHECK(log.CountToolResults() == 1);

	auto tr = log.LastToolResult();
	REQUIRE(tr.has_value());
	CHECK(tr->result.content == "hi");
	CHECK_FALSE(tr->result.isError);
}

TEST_CASE("Loop: multiple tool calls in one step")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "echo", R"({"msg":"first"})"),
					   TC(1, "c2", "echo", R"({"msg":"second"})")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "All done", .finish = llm::FinishReason::Completed},
	};

	MockTool echo;
	echo.toolName = "echo";
	echo.handler = [](const json& args) -> eng::ToolResult {
		return {.content = args.value("msg", "")};
	};

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&echo},
		.dispatchEvent = log.MakeDispatcher(),
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stepsExecuted == 2);
	CHECK(log.CountToolResults() == 2);

	REQUIRE(result.updatedHistory.size() >= 4);
	auto& toolMsg1 = result.updatedHistory[result.updatedHistory.size() - 3];
	auto& toolMsg2 = result.updatedHistory[result.updatedHistory.size() - 2];
	CHECK(toolMsg1.role == llm::Role::Tool);
	CHECK(toolMsg2.role == llm::Role::Tool);
}

TEST_CASE("Loop: multi-step tool chain")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "step1", "{}")}, .finish = llm::FinishReason::ToolCalls},
		{.toolCalls = {TC(0, "c2", "step2", "{}")}, .finish = llm::FinishReason::ToolCalls},
		{.text = "Final answer", .finish = llm::FinishReason::Completed},
	};

	MockTool step1, step2;
	step1.toolName = "step1";
	step2.toolName = "step2";

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&step1, &step2},
		.dispatchEvent = log.MakeDispatcher(),
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stepsExecuted == 3);
	CHECK(result.stopReason == eng::StopReason::Completed);
	CHECK(log.CountToolResults() == 2);
}

TEST_CASE("Loop: max steps limit")
{
	MockChatProvider provider;
	for (int i = 0; i < 10; ++i)
	{
		provider.responses.push_back(
			{.toolCalls = {TC(0, "c" + std::to_string(i), "loop_tool", "{}")},
			 .finish = llm::FinishReason::ToolCalls});
	}

	MockTool loopTool;
	loopTool.toolName = "loop_tool";

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&loopTool},
		.dispatchEvent = log.MakeDispatcher(),
		.maxSteps = 3,
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::MaxSteps);
	CHECK(result.stepsExecuted == 3);
}

TEST_CASE("Loop: cancellation via stopToken")
{
	std::stop_source stopSource;
	stopSource.request_stop();

	MockChatProvider provider;
	provider.responses = {
		{.text = "should not reach", .finish = llm::FinishReason::Completed},
	};

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.dispatchEvent = log.MakeDispatcher(),
		.stopToken = stopSource.get_token(),
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Aborted);
	CHECK(result.stepsExecuted == 0);
}

TEST_CASE("Loop: LLM error returns kError")
{
	MockChatProvider provider;
	provider.responses = {};

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.dispatchEvent = log.MakeDispatcher(),
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Error);
	CHECK_FALSE(result.errorMessage.empty());
}

TEST_CASE("Loop: tool not found produces error result, loop continues")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "nonexistent", "{}")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "Recovered", .finish = llm::FinishReason::Completed},
	};

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.dispatchEvent = log.MakeDispatcher(),
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Completed);
	CHECK(log.CountToolResults() == 1);

	auto tr = log.LastToolResult();
	REQUIRE(tr.has_value());
	CHECK(tr->result.isError);
	CHECK(tr->result.content.find("not found") != std::string::npos);
}

TEST_CASE("Loop: tool execution error becomes error result in history")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "failing_tool", "{}")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "Handled error", .finish = llm::FinishReason::Completed},
	};

	MockTool failing;
	failing.toolName = "failing_tool";
	failing.handler = [](const json&) -> eng::ToolResult {
		return {.content = "permission denied", .isError = true};
	};

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&failing},
		.dispatchEvent = log.MakeDispatcher(),
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Completed);
	auto tr = log.LastToolResult();
	REQUIRE(tr.has_value());
	CHECK(tr->result.isError);
	CHECK(tr->result.content == "permission denied");
}

TEST_CASE("Loop: usage accumulates across steps")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "t", "{}")},
		 .finish = llm::FinishReason::ToolCalls,
		 .usage = {.inputOther = 100, .output = 10}},
		{.text = "done",
		 .finish = llm::FinishReason::Completed,
		 .usage = {.inputOther = 120, .output = 5}},
	};

	MockTool t;
	t.toolName = "t";

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&t},
		.dispatchEvent = log.MakeDispatcher(),
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.totalUsage.inputOther == 220);
	CHECK(result.totalUsage.output == 15);
}

TEST_CASE("Loop: history accumulates assistant and tool messages")
{
	MockChatProvider provider;
	provider.responses = {
		{.text = "thinking",
		 .toolCalls = {TC(0, "c1", "t", "{}")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "final answer", .finish = llm::FinishReason::Completed},
	};

	MockTool t;
	t.toolName = "t";

	llm::Message userMsg;
	userMsg.role = llm::Role::User;
	userMsg.content.push_back(llm::TextPart{"initial question"});

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&t},
		.history = {userMsg},
		.dispatchEvent = log.MakeDispatcher(),
	};

	auto result = eng::RunTurn(std::move(input));

	REQUIRE(result.updatedHistory.size() == 4);
	CHECK(result.updatedHistory[0].role == llm::Role::User);
	CHECK(result.updatedHistory[1].role == llm::Role::Assistant);
	CHECK(result.updatedHistory[2].role == llm::Role::Tool);
	CHECK(result.updatedHistory[3].role == llm::Role::Assistant);

	auto& finalAssistant = result.updatedHistory[3];
	REQUIRE_FALSE(finalAssistant.content.empty());
	auto* text = std::get_if<llm::TextPart>(&finalAssistant.content[0]);
	REQUIRE(text);
	CHECK(text->text == "final answer");
}

TEST_CASE("Loop: hooks are called")
{
	MockChatProvider provider;
	provider.responses = {
		{.text = "hello", .finish = llm::FinishReason::Completed},
	};

	int beforeCount = 0;
	int afterCount = 0;

	eng::LoopHooks hooks{
		.beforeStep = [&](int) { ++beforeCount; },
		.afterStep = [&](int) { ++afterCount; },
	};

	eng::TurnInput input{.provider = &provider};

	eng::RunTurn(std::move(input), hooks);

	CHECK(beforeCount == 1);
	CHECK(afterCount == 1);
}

TEST_CASE("Loop: no tools means single-step text response")
{
	MockChatProvider provider;
	provider.responses = {
		{.text = "I have no tools", .finish = llm::FinishReason::Completed},
	};

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.dispatchEvent = log.MakeDispatcher(),
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stepsExecuted == 1);
	CHECK(result.stopReason == eng::StopReason::Completed);
	CHECK(log.CountAssistantDeltas() == 1);
}