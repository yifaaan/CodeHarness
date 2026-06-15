#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "absl/status/status.h"
#include "fmt/format.h"

#include "Engine/Loop.h"
#include "Engine/LoopTypes.h"
#include "Engine/Tool.h"
#include "Llm/ChatProvider.h"
#include "Llm/Types.h"
#include "Permission/PermissionGate.h"

#include "Config/ConfigTypes.h"

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
		bool canRunConcurrently = false;
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
			return eng::ToolExecution{.description = fmt::format("execute {}", toolName), .canRunConcurrently = canRunConcurrently};
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

		std::vector<eng::ToolResultEvent> ToolResults() const
		{
			std::vector<eng::ToolResultEvent> result;
			for (const auto& e : events)
			{
				if (auto* tr = std::get_if<eng::ToolResultEvent>(&e))
					result.push_back(*tr);
			}
			return result;
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

	void UpdateMax(std::atomic<int>& maxValue, int candidate)
	{
		int current = maxValue.load();
		while (candidate > current && !maxValue.compare_exchange_weak(current, candidate))
		{
		}
	}

	// Variant of MockTool whose ResolveExecution sets requiresPermission, so
	// permission-gated loop tests can exercise the mutating-tool path.
	class GatedTool : public eng::ExecutableTool
	{
	public:
		std::string toolName;
		std::string toolDesc;
		bool requiresPermission = true;
		bool canRunConcurrently = false;
		bool executeCalled = false;
		std::function<eng::ToolResult(const json&)> handler;

		std::string Name() const override { return toolName; }
		std::string Description() const override { return toolDesc; }
		json Parameters() const override { return json::object(); }

		absl::StatusOr<eng::ToolExecution> ResolveExecution(const json&) override
		{
			return eng::ToolExecution{
				.description = fmt::format("execute {}", toolName),
				.requiresPermission = requiresPermission,
				.canRunConcurrently = canRunConcurrently,
			};
		}

		absl::StatusOr<eng::ToolResult> Execute(const json& args, const eng::ToolContext&) override
		{
			executeCalled = true;
			if (handler)
				return handler(args);
			return eng::ToolResult{.content = "ok", .isError = false};
		}
	};

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

TEST_CASE("Loop: concurrent-safe tools run in parallel and results stay ordered")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "read", R"({"msg":"first"})"),
					   TC(1, "c2", "read", R"({"msg":"second"})")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "All done", .finish = llm::FinishReason::Completed},
	};

	std::atomic<int> active = 0;
	std::atomic<int> maxActive = 0;
	int started = 0;
	std::mutex mutex;
	std::condition_variable cv;

	MockTool read;
	read.toolName = "read";
	read.canRunConcurrently = true;
	read.handler = [&](const json& args) -> eng::ToolResult {
		int now = ++active;
		UpdateMax(maxActive, now);
		{
			std::lock_guard lock(mutex);
			++started;
		}
		cv.notify_all();
		{
			std::unique_lock lock(mutex);
			cv.wait_for(lock, std::chrono::seconds(2), [&] { return started >= 2; });
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		--active;
		return {.content = args.value("msg", "")};
	};

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&read},
		.dispatchEvent = log.MakeDispatcher(),
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Completed);
	CHECK(maxActive.load() >= 2);

	auto toolResults = log.ToolResults();
	REQUIRE(toolResults.size() == 2);
	CHECK(toolResults[0].id == "c1");
	CHECK(toolResults[0].result.content == "first");
	CHECK(toolResults[1].id == "c2");
	CHECK(toolResults[1].result.content == "second");

	REQUIRE(result.updatedHistory.size() >= 4);
	auto& toolMsg1 = result.updatedHistory[result.updatedHistory.size() - 3];
	auto& toolMsg2 = result.updatedHistory[result.updatedHistory.size() - 2];
	CHECK(toolMsg1.toolCallId == "c1");
	CHECK(toolMsg2.toolCallId == "c2");
}

TEST_CASE("Loop: scheduler maxConcurrentTools <= 1 keeps concurrent-safe tools serial")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "read", "{}"),
					   TC(1, "c2", "read", "{}")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "done", .finish = llm::FinishReason::Completed},
	};

	std::atomic<int> active = 0;
	std::atomic<int> maxActive = 0;

	MockTool read;
	read.toolName = "read";
	read.canRunConcurrently = true;
	read.handler = [&](const json&) -> eng::ToolResult {
		int now = ++active;
		UpdateMax(maxActive, now);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		--active;
		return {.content = "ok"};
	};

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&read},
		.dispatchEvent = log.MakeDispatcher(),
		.toolScheduler = {.maxConcurrentTools = 1},
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Completed);
	CHECK(maxActive.load() == 1);
	CHECK(log.CountToolResults() == 2);
}

TEST_CASE("Loop: serial tools act as barriers between concurrent-safe batches")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "read", R"({"msg":"before"})"),
					   TC(1, "c2", "edit", "{}"),
					   TC(2, "c3", "read", R"({"msg":"after"})")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "done", .finish = llm::FinishReason::Completed},
	};

	std::atomic<int> activeReads = 0;
	bool editOverlappedRead = false;
	std::vector<std::string> order;
	std::mutex mutex;

	MockTool read;
	read.toolName = "read";
	read.canRunConcurrently = true;
	read.handler = [&](const json& args) -> eng::ToolResult {
		++activeReads;
		{
			std::lock_guard lock(mutex);
			order.push_back("read-start-" + args.value("msg", ""));
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		{
			std::lock_guard lock(mutex);
			order.push_back("read-end-" + args.value("msg", ""));
		}
		--activeReads;
		return {.content = args.value("msg", "")};
	};

	MockTool edit;
	edit.toolName = "edit";
	edit.canRunConcurrently = false;
	edit.handler = [&](const json&) -> eng::ToolResult {
		editOverlappedRead = activeReads.load() != 0;
		{
			std::lock_guard lock(mutex);
			order.push_back("edit");
		}
		return {.content = "edited"};
	};

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&read, &edit},
		.dispatchEvent = log.MakeDispatcher(),
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Completed);
	CHECK_FALSE(editOverlappedRead);
	REQUIRE(order.size() == 5);
	CHECK(order[0] == "read-start-before");
	CHECK(order[1] == "read-end-before");
	CHECK(order[2] == "edit");
	CHECK(order[3] == "read-start-after");
	CHECK(order[4] == "read-end-after");
}

TEST_CASE("Loop: concurrent batch preserves order when one tool fails")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "read", R"({"msg":"ok"})"),
					   TC(1, "c2", "read", R"({"msg":"fail"})")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "done", .finish = llm::FinishReason::Completed},
	};

	MockTool read;
	read.toolName = "read";
	read.canRunConcurrently = true;
	read.handler = [](const json& args) -> eng::ToolResult {
		if (args.value("msg", "") == "fail")
			return {.content = "boom", .isError = true};
		return {.content = "ok"};
	};

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&read},
		.dispatchEvent = log.MakeDispatcher(),
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Completed);
	auto toolResults = log.ToolResults();
	REQUIRE(toolResults.size() == 2);
	CHECK(toolResults[0].id == "c1");
	CHECK_FALSE(toolResults[0].result.isError);
	CHECK(toolResults[1].id == "c2");
	CHECK(toolResults[1].result.isError);
}

TEST_CASE("Loop: invalid concurrent tool arguments do not execute")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "read", "{not json}")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "recovered", .finish = llm::FinishReason::Completed},
	};

	MockTool read;
	read.toolName = "read";
	read.canRunConcurrently = true;
	bool executeCalled = false;
	read.handler = [&](const json&) -> eng::ToolResult {
		executeCalled = true;
		return {.content = "should not run"};
	};

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&read},
		.dispatchEvent = log.MakeDispatcher(),
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Completed);
	CHECK_FALSE(executeCalled);
	auto tr = log.LastToolResult();
	REQUIRE(tr.has_value());
	CHECK(tr->result.isError);
	CHECK(tr->result.content.find("invalid tool arguments") != std::string::npos);
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

TEST_CASE("Loop: null permission gate allows mutating tools (back-compat)")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "write_file", "{}")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "done", .finish = llm::FinishReason::Completed},
	};

	GatedTool write;
	write.toolName = "write_file";

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&write},
		.dispatchEvent = log.MakeDispatcher(),
		// permissionGate left null — legacy allow-all behavior
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Completed);
	CHECK(write.executeCalled); // the tool actually ran
	CHECK(log.CountToolResults() == 1);
	auto tr = log.LastToolResult();
	REQUIRE(tr.has_value());
	CHECK_FALSE(tr->result.isError);
}

TEST_CASE("Loop: gate in Manual mode denies mutating tool, Execute is not called")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "write_file", R"({"path":"a.txt"})")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "recovered", .finish = llm::FinishReason::Completed},
	};

	GatedTool write;
	write.toolName = "write_file";

	// Callback returns Deny for everything.
	codeharness::permission::PermissionGate gate(
		codeharness::config::PermissionMode::Manual,
		[](std::string_view, const json&, std::string_view) {
			return codeharness::permission::PermissionDecision::Deny;
		});

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&write},
		.dispatchEvent = log.MakeDispatcher(),
		.permissionGate = &gate,
	};

	auto result = eng::RunTurn(std::move(input));

	// The turn continues (loop recovers from the denied tool), but the tool
	// itself never executed.
	CHECK(result.stopReason == eng::StopReason::Completed);
	CHECK_FALSE(write.executeCalled);

	auto tr = log.LastToolResult();
	REQUIRE(tr.has_value());
	CHECK(tr->result.isError);
	CHECK(tr->result.content.find("permission denied") != std::string::npos);
}

TEST_CASE("Loop: gate in Manual mode allows mutating tool when callback approves")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "bash", "{}")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "ok", .finish = llm::FinishReason::Completed},
	};

	GatedTool bash;
	bash.toolName = "bash";

	int approvals = 0;
	codeharness::permission::PermissionGate gate(
		codeharness::config::PermissionMode::Manual,
		[&approvals](std::string_view, const json&, std::string_view) {
			++approvals;
			return codeharness::permission::PermissionDecision::Allow;
		});

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&bash},
		.dispatchEvent = log.MakeDispatcher(),
		.permissionGate = &gate,
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Completed);
	CHECK(bash.executeCalled);
	CHECK(approvals == 1);
}

TEST_CASE("Loop: gate in Yolo mode allows mutating tool without invoking callback")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "write_file", "{}")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "done", .finish = llm::FinishReason::Completed},
	};

	GatedTool write;
	write.toolName = "write_file";

	int callbacks = 0;
	codeharness::permission::PermissionGate gate(
		codeharness::config::PermissionMode::Yolo,
		[&callbacks](std::string_view, const json&, std::string_view) {
			++callbacks;
			return codeharness::permission::PermissionDecision::Allow;
		});

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&write},
		.dispatchEvent = log.MakeDispatcher(),
		.permissionGate = &gate,
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Completed);
	CHECK(write.executeCalled);
	CHECK(callbacks == 0); // Yolo never consults the callback
}

TEST_CASE("Loop: read-only tools run even with a denying gate")
{
	MockChatProvider provider;
	provider.responses = {
		{.toolCalls = {TC(0, "c1", "read", "{}")},
		 .finish = llm::FinishReason::ToolCalls},
		{.text = "done", .finish = llm::FinishReason::Completed},
	};

	GatedTool read;
	read.toolName = "read";
	read.requiresPermission = false; // read-only

	codeharness::permission::PermissionGate gate(
		codeharness::config::PermissionMode::Manual,
		[](std::string_view, const json&, std::string_view) {
			return codeharness::permission::PermissionDecision::Deny;
		});

	EventLog log;
	eng::TurnInput input{
		.provider = &provider,
		.tools = {&read},
		.dispatchEvent = log.MakeDispatcher(),
		.permissionGate = &gate,
	};

	auto result = eng::RunTurn(std::move(input));

	CHECK(result.stopReason == eng::StopReason::Completed);
	CHECK(read.executeCalled); // read-only tools bypass the gate entirely
}
