#include "Agent/Agent.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "Engine/Tool.h"
#include "Host/LocalHost.h"
#include "Llm/ChatProvider.h"
#include "Records/AgentRecords.h"
#include "Records/FilePersistence.h"
#include "Records/RecordTypes.h"
#include "Tools/ToolManager.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace agent = codeharness::agent;
namespace engine = codeharness::engine;
namespace llm = codeharness::llm;
namespace tools = codeharness::tools;
namespace records = codeharness::records;
namespace host = codeharness::host;
using json = nlohmann::json;

namespace
{

	struct CannedResponse
	{
		std::string text;
		struct ToolCallSpec
		{
			int index = 0;
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
		std::vector<std::vector<llm::Message>> histories;
		std::vector<std::vector<std::string>> toolNames;
		std::string lastSystemPrompt;
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

		absl::Status Generate(std::string_view systemPrompt,
							  std::span<const llm::Tool> tools,
							  std::span<const llm::Message> history,
							  const llm::StreamCallbacks& callbacks,
							  std::stop_token stopToken = {}) override
		{
			lastSystemPrompt = std::string(systemPrompt);
			histories.emplace_back(history.begin(), history.end());

			auto& names = toolNames.emplace_back();
			for (const auto& tool : tools)
			{
				names.push_back(tool.name);
			}

			if (stopToken.stop_requested())
			{
				return absl::OkStatus();
			}

			size_t idx = callCount++;
			if (idx >= responses.size())
			{
				return absl::InternalError("no more canned responses");
			}

			const auto& response = responses[idx];
			if (!response.text.empty() && callbacks.onText)
			{
				callbacks.onText(response.text);
			}

			for (const auto& toolCall : response.toolCalls)
			{
				if (callbacks.onToolCallStart)
				{
					callbacks.onToolCallStart(toolCall.index, toolCall.id, toolCall.name);
				}
				if (!toolCall.arguments.empty() && callbacks.onToolCallDelta)
				{
					callbacks.onToolCallDelta(toolCall.index, toolCall.arguments);
				}
			}

			if (callbacks.onFinish)
			{
				callbacks.onFinish(response.finish, response.usage);
			}

			return absl::OkStatus();
		}
	};

	class StubTool : public engine::ExecutableTool
	{
	public:
		explicit StubTool(std::string name, std::function<engine::ToolResult(const json&)> handler = {})
			: name(std::move(name)), handler(std::move(handler))
		{
		}

		std::string Name() const override
		{
			return name;
		}

		std::string Description() const override
		{
			return "stub";
		}

		json Parameters() const override
		{
			return json::object();
		}

		absl::StatusOr<engine::ToolExecution> ResolveExecution(const json&) override
		{
			return engine::ToolExecution{.description = "stub"};
		}

		absl::StatusOr<engine::ToolResult> Execute(const json& args, const engine::ToolContext&) override
		{
			if (handler)
			{
				return handler(args);
			}
			return engine::ToolResult{.content = "stub"};
		}

	private:
		std::string name;
		std::function<engine::ToolResult(const json&)> handler;
	};

	CannedResponse::ToolCallSpec ToolCall(int index, std::string id, std::string name, std::string args = "{}")
	{
		return {.index = index, .id = std::move(id), .name = std::move(name), .arguments = std::move(args)};
	}

	int CountLoopEvents(const std::vector<agent::AgentEvent>& events)
	{
		return static_cast<int>(std::count_if(events.begin(), events.end(), [](const auto& event) {
			return std::holds_alternative<agent::LoopEvent>(event);
		}));
	}

} // namespace

TEST_CASE("Agent: construction reports idle status and default config")
{
	MockChatProvider provider;
	agent::Agent harness(&provider);

	CHECK(harness.GetStatus() == agent::AgentStatus::Idle);
	CHECK(harness.GetConfig().maxSteps == 1000);
	CHECK(harness.GetHistory().empty());
	CHECK(harness.GetActiveTools().empty());
}

TEST_CASE("Agent: Prompt appends user and assistant messages")
{
	MockChatProvider provider;
	provider.responses = {{.text = "hello", .finish = llm::FinishReason::Completed}};

	agent::Agent harness(&provider);
	auto result = harness.Prompt("hi");

	REQUIRE(result.ok());
	CHECK(result->stopReason == engine::StopReason::Completed);

	const auto& history = harness.GetHistory();
	REQUIRE(history.size() == 2);
	CHECK(history[0].role == llm::Role::User);
	CHECK(history[1].role == llm::Role::Assistant);

	auto* text = std::get_if<llm::TextPart>(&history[1].content[0]);
	REQUIRE(text != nullptr);
	CHECK(text->text == "hello");
}

TEST_CASE("Agent: multi-turn prompts preserve prior history")
{
	MockChatProvider provider;
	provider.responses = {
		{.text = "one", .finish = llm::FinishReason::Completed},
		{.text = "two", .finish = llm::FinishReason::Completed},
	};

	agent::Agent harness(&provider);
	REQUIRE(harness.Prompt("first").ok());
	REQUIRE(harness.Prompt("second").ok());

	REQUIRE(provider.histories.size() == 2);
	CHECK(provider.histories[0].size() == 1);
	CHECK(provider.histories[1].size() == 3);
	CHECK(harness.GetHistory().size() == 4);
}

TEST_CASE("Agent: forwards loop and lifecycle events")
{
	MockChatProvider provider;
	provider.responses = {{.text = "hello", .finish = llm::FinishReason::Completed}};

	std::vector<agent::AgentEvent> events;
	agent::Agent harness(&provider);
	harness.SetEventDispatcher([&](const agent::AgentEvent& event) { events.push_back(event); });

	REQUIRE(harness.Prompt("hi").ok());

	CHECK(std::any_of(events.begin(), events.end(), [](const auto& event) { return std::holds_alternative<agent::TurnStartedEvent>(event); }));
	CHECK(std::any_of(events.begin(), events.end(), [](const auto& event) { return std::holds_alternative<agent::TurnEndedEvent>(event); }));
	CHECK(std::any_of(events.begin(), events.end(), [](const auto& event) { return std::holds_alternative<agent::StatusChangedEvent>(event); }));
	CHECK(CountLoopEvents(events) >= 2);
}

TEST_CASE("Agent: active tools filter ToolManager loop tools")
{
	MockChatProvider provider;
	provider.responses = {{.text = "ok", .finish = llm::FinishReason::Completed}};

	tools::ToolManager manager;
	manager.Register(std::make_unique<StubTool>("alpha"));
	manager.Register(std::make_unique<StubTool>("beta"));

	agent::Agent harness(&provider, nullptr, &manager);
	CHECK(harness.SetActiveTools({"alpha"}).ok());

	auto result = harness.Prompt("use a tool if needed");
	REQUIRE(result.ok());

	REQUIRE(provider.toolNames.size() == 1);
	REQUIRE(provider.toolNames[0].size() == 1);
	CHECK(provider.toolNames[0][0] == "alpha");

	auto bad = harness.SetActiveTools({"missing"});
	CHECK_FALSE(bad.ok());
	CHECK(bad.code() == absl::StatusCode::kInvalidArgument);
}

TEST_CASE("Agent: default tool profile uses all registered tools")
{
	MockChatProvider provider;
	provider.responses = {{.text = "ok", .finish = llm::FinishReason::Completed}};

	tools::ToolManager manager;
	manager.Register(std::make_unique<StubTool>("alpha"));
	manager.Register(std::make_unique<StubTool>("beta"));

	agent::Agent harness(&provider, nullptr, &manager);
	auto result = harness.Prompt("hi");

	REQUIRE(result.ok());
	REQUIRE(provider.toolNames.size() == 1);
	const std::vector<std::string> expectedTools{"alpha", "beta"};
	CHECK(provider.toolNames[0] == expectedTools);
}

TEST_CASE("Agent: ClearContext empties history")
{
	MockChatProvider provider;
	provider.responses = {{.text = "hello", .finish = llm::FinishReason::Completed}};

	agent::Agent harness(&provider);
	REQUIRE(harness.Prompt("hi").ok());
	REQUIRE_FALSE(harness.GetHistory().empty());

	harness.ClearContext();
	CHECK(harness.GetHistory().empty());
}

TEST_CASE("Agent: cancellation requested at turn start aborts safely")
{
	MockChatProvider provider;
	provider.responses = {{.text = "should not appear", .finish = llm::FinishReason::Completed}};

	agent::Agent* harnessPtr = nullptr;
	agent::Agent harness(&provider);
	harnessPtr = &harness;
	harness.SetEventDispatcher([&](const agent::AgentEvent& event) {
		if (std::holds_alternative<agent::TurnStartedEvent>(event))
		{
			harnessPtr->Cancel();
		}
	});

	auto result = harness.Prompt("stop");
	REQUIRE(result.ok());
	CHECK(result->stopReason == engine::StopReason::Aborted);
	CHECK(harness.GetStatus() == agent::AgentStatus::Idle);

	const auto& history = harness.GetHistory();
	REQUIRE(history.size() == 1);
	CHECK(history[0].role == llm::Role::User);
}

namespace
{

	struct RecordsTmpFixture
	{
		host::LocalHost host;
		std::filesystem::path tmpDir;
		std::string wirePath;

		RecordsTmpFixture()
		{
			auto tmpBase = std::filesystem::temp_directory_path();
			tmpDir = tmpBase / ("codeharness_agent_records_test_" + std::to_string(std::time(nullptr)));
			std::filesystem::create_directories(tmpDir);
			CHECK(host.Chdir(tmpDir.string()).ok());
			wirePath = (tmpDir / "wire.jsonl").string();
		}

		~RecordsTmpFixture()
		{
			std::error_code ec;
			std::filesystem::remove_all(tmpDir, ec);
		}

		std::unique_ptr<records::AgentRecords> MakeRecords()
		{
			return std::make_unique<records::AgentRecords>(
				std::make_unique<records::FilePersistence>(&host, wirePath));
		}
	};

} // namespace

TEST_CASE("Agent: Prompt with records writes turn.prompt + append_message + loop events")
{
	RecordsTmpFixture f;
	MockChatProvider provider;
	provider.responses = {{.text = "hello", .finish = llm::FinishReason::Completed}};

	auto ar = f.MakeRecords();
	agent::Agent harness(&provider);
	harness.SetRecords(ar.get());

	REQUIRE(harness.Prompt("hi").ok());

	auto all = ar->ReadAll();
	REQUIRE(all.ok());
	CHECK_GT(all->size(), 2u);

	CHECK(std::holds_alternative<records::TurnPromptRecord>((*all)[0].record));
	CHECK(std::holds_alternative<records::ContextAppendMessageRecord>((*all)[1].record));

	bool sawLoopEvent = false;
	for (const auto& w : *all)
	{
		if (std::holds_alternative<records::ContextAppendLoopEventRecord>(w.record))
		{
			sawLoopEvent = true;
			break;
		}
	}
	CHECK(sawLoopEvent);
}

TEST_CASE("Agent: Resume replays history from records sink")
{
	RecordsTmpFixture f;
	MockChatProvider provider;
	provider.responses = {
		{.text = "first answer", .finish = llm::FinishReason::Completed},
		{.text = "second answer", .finish = llm::FinishReason::Completed},
	};

	// Agent A: drives two turns with records wired.
	auto arA = f.MakeRecords();
	{
		agent::Agent a(&provider);
		a.SetRecords(arA.get());
		REQUIRE(a.Prompt("question 1").ok());
		REQUIRE(a.Prompt("question 2").ok());
		REQUIRE_EQ(a.GetHistory().size(), 4u);
	}

	// Agent B: fresh in-memory state, same wire.jsonl. Resume should restore
	// the 2 user messages (assistant text isn't recorded by the minimal set;
	// see Records::ContextAppendMessageRecord for the rationale).
	auto arB = f.MakeRecords();
	MockChatProvider providerB;
	agent::Agent b(&providerB);
	b.SetRecords(arB.get());

	auto status = b.Resume();
	REQUIRE(status.ok());

	const auto& rebuilt = b.GetHistory();
	REQUIRE_EQ(rebuilt.size(), 2u);
	CHECK(rebuilt[0].role == llm::Role::User);
	CHECK(rebuilt[1].role == llm::Role::User);

	auto* t1 = std::get_if<llm::TextPart>(&rebuilt[0].content[0]);
	REQUIRE(t1 != nullptr);
	CHECK_EQ(t1->text, "question 1");
	auto* t2 = std::get_if<llm::TextPart>(&rebuilt[1].content[0]);
	REQUIRE(t2 != nullptr);
	CHECK_EQ(t2->text, "question 2");
}

TEST_CASE("Agent: Resume with no records sink fails")
{
	MockChatProvider provider;
	agent::Agent harness(&provider);
	auto status = harness.Resume();
	CHECK_FALSE(status.ok());
	CHECK(absl::IsFailedPrecondition(status));
}

TEST_CASE("Agent: records is best-effort — provider errors still propagate via stopReason")
{
	RecordsTmpFixture f;
	MockChatProvider provider;
	// No responses queued -> Generate returns InternalError on first call.

	auto ar = f.MakeRecords();
	agent::Agent harness(&provider);
	harness.SetRecords(ar.get());

	auto result = harness.Prompt("will fail");
	REQUIRE(result.ok());
	CHECK(result->stopReason == engine::StopReason::Error);

	// Even on error, the turn.prompt + initial append_message should have been
	// recorded (they happen before RunTurn is invoked).
	auto all = ar->ReadAll();
	REQUIRE(all.ok());
	CHECK_GE(all->size(), 2u);
	CHECK(std::holds_alternative<records::TurnPromptRecord>((*all)[0].record));
}
