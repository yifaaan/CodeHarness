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

namespace {

struct CannedResponse {
  std::string text;
  struct ToolCallSpec {
    int index;
    std::string id;
    std::string name;
    std::string arguments;
  };
  std::vector<ToolCallSpec> tool_calls;
  llm::FinishReason finish = llm::FinishReason::kCompleted;
  llm::TokenUsage usage{};
};

class MockChatProvider : public llm::ChatProvider {
 public:
  std::vector<CannedResponse> responses;
  size_t call_count = 0;

  std::string Name() const override { return "mock"; }
  std::string ModelName() const override { return "mock-model"; }
  std::optional<llm::ThinkingEffort> ThinkingEffortLevel() const override { return std::nullopt; }

  absl::Status Generate(std::string_view system_prompt, std::span<const llm::Tool> tools,
                        std::span<const llm::Message> history, const llm::StreamCallbacks& callbacks,
                        std::stop_token stop_token = {}) override {
    size_t idx = call_count++;
    if (idx >= responses.size()) {
      return absl::InternalError("no more canned responses");
    }

    const auto& resp = responses[idx];

    if (!resp.text.empty() && callbacks.on_text) {
      callbacks.on_text(resp.text);
    }

    for (const auto& tc : resp.tool_calls) {
      if (callbacks.on_tool_call_start) {
        callbacks.on_tool_call_start(tc.index, tc.id, tc.name);
      }
      if (!tc.arguments.empty() && callbacks.on_tool_call_delta) {
        callbacks.on_tool_call_delta(tc.index, tc.arguments);
      }
    }

    if (callbacks.on_finish) {
      callbacks.on_finish(resp.finish, resp.usage);
    }

    return absl::OkStatus();
  }
};

class MockTool : public eng::ExecutableTool {
 public:
  std::string tool_name;
  std::string tool_desc;
  json tool_params = json::object();
  std::function<eng::ToolResult(const json&)> handler;

  std::string Name() const override { return tool_name; }
  std::string Description() const override { return tool_desc; }
  json Parameters() const override { return tool_params; }

  absl::StatusOr<eng::ToolExecution> ResolveExecution(const json& args) override {
    return eng::ToolExecution{.description = fmt::format("execute {}", tool_name)};
  }

  absl::StatusOr<eng::ToolResult> Execute(const json& args, const eng::ToolContext& ctx) override {
    if (handler) return handler(args);
    return eng::ToolResult{.content = "ok", .is_error = false};
  }
};

struct EventLog {
  std::vector<eng::LoopEvent> events;

  int CountStepStarted() const {
    return std::count_if(events.begin(), events.end(), [](const auto& e) {
      return std::holds_alternative<eng::StepStartedEvent>(e);
    });
  }

  int CountAssistantDeltas() const {
    return std::count_if(events.begin(), events.end(), [](const auto& e) {
      return std::holds_alternative<eng::AssistantDeltaEvent>(e);
    });
  }

  int CountToolResults() const {
    return std::count_if(events.begin(), events.end(), [](const auto& e) {
      return std::holds_alternative<eng::ToolResultEvent>(e);
    });
  }

  std::optional<eng::ToolResultEvent> LastToolResult() const {
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
      if (auto* tr = std::get_if<eng::ToolResultEvent>(&*it)) return *tr;
    }
    return std::nullopt;
  }

  std::string AllText() const {
    std::string result;
    for (const auto& e : events) {
      if (auto* d = std::get_if<eng::AssistantDeltaEvent>(&e)) {
        result += d->text;
      }
    }
    return result;
  }

  eng::EventDispatcher MakeDispatcher() {
    return [this](const eng::LoopEvent& e) { events.push_back(e); };
  }
};

CannedResponse::ToolCallSpec TC(int idx, std::string id, std::string name, std::string args = "{}") {
  return {idx, std::move(id), std::move(name), std::move(args)};
}

}  // namespace

TEST_CASE("Loop: text-only response completes in 1 step") {
  MockChatProvider provider;
  provider.responses = {
      {.text = "Hello!", .finish = llm::FinishReason::kCompleted},
  };

  EventLog log;
  eng::TurnInput input{
      .provider = &provider,
      .system_prompt = "be helpful",
      .dispatch_event = log.MakeDispatcher(),
  };

  auto result = eng::RunTurn(std::move(input));

  CHECK(result.stop_reason == eng::StopReason::kCompleted);
  CHECK(result.steps_executed == 1);
  CHECK(log.AllText() == "Hello!");
  CHECK(log.CountStepStarted() == 1);
}

TEST_CASE("Loop: single tool call then text completes in 2 steps") {
  MockChatProvider provider;
  provider.responses = {
      {.tool_calls = {TC(0, "call_1", "echo", R"({"msg":"hi"})")},
       .finish = llm::FinishReason::kToolCalls},
      {.text = "Done!", .finish = llm::FinishReason::kCompleted},
  };

  MockTool echo;
  echo.tool_name = "echo";
  echo.handler = [](const json& args) -> eng::ToolResult {
    return {.content = args.value("msg", "no msg")};
  };

  EventLog log;
  eng::TurnInput input{
      .provider = &provider,
      .tools = {&echo},
      .dispatch_event = log.MakeDispatcher(),
  };

  auto result = eng::RunTurn(std::move(input));

  CHECK(result.stop_reason == eng::StopReason::kCompleted);
  CHECK(result.steps_executed == 2);
  CHECK(log.AllText() == "Done!");
  CHECK(log.CountToolResults() == 1);

  auto tr = log.LastToolResult();
  REQUIRE(tr.has_value());
  CHECK(tr->result.content == "hi");
  CHECK_FALSE(tr->result.is_error);
}

TEST_CASE("Loop: multiple tool calls in one step") {
  MockChatProvider provider;
  provider.responses = {
      {.tool_calls = {TC(0, "c1", "echo", R"({"msg":"first"})"),
                      TC(1, "c2", "echo", R"({"msg":"second"})")},
       .finish = llm::FinishReason::kToolCalls},
      {.text = "All done", .finish = llm::FinishReason::kCompleted},
  };

  MockTool echo;
  echo.tool_name = "echo";
  echo.handler = [](const json& args) -> eng::ToolResult {
    return {.content = args.value("msg", "")};
  };

  EventLog log;
  eng::TurnInput input{
      .provider = &provider,
      .tools = {&echo},
      .dispatch_event = log.MakeDispatcher(),
  };

  auto result = eng::RunTurn(std::move(input));

  CHECK(result.steps_executed == 2);
  CHECK(log.CountToolResults() == 2);

  REQUIRE(result.updated_history.size() >= 4);
  auto& tool_msg1 = result.updated_history[result.updated_history.size() - 3];
  auto& tool_msg2 = result.updated_history[result.updated_history.size() - 2];
  CHECK(tool_msg1.role == llm::Role::kTool);
  CHECK(tool_msg2.role == llm::Role::kTool);
}

TEST_CASE("Loop: multi-step tool chain") {
  MockChatProvider provider;
  provider.responses = {
      {.tool_calls = {TC(0, "c1", "step1", "{}")}, .finish = llm::FinishReason::kToolCalls},
      {.tool_calls = {TC(0, "c2", "step2", "{}")}, .finish = llm::FinishReason::kToolCalls},
      {.text = "Final answer", .finish = llm::FinishReason::kCompleted},
  };

  MockTool step1, step2;
  step1.tool_name = "step1";
  step2.tool_name = "step2";

  EventLog log;
  eng::TurnInput input{
      .provider = &provider,
      .tools = {&step1, &step2},
      .dispatch_event = log.MakeDispatcher(),
  };

  auto result = eng::RunTurn(std::move(input));

  CHECK(result.steps_executed == 3);
  CHECK(result.stop_reason == eng::StopReason::kCompleted);
  CHECK(log.CountToolResults() == 2);
}

TEST_CASE("Loop: max steps limit") {
  MockChatProvider provider;
  for (int i = 0; i < 10; ++i) {
    provider.responses.push_back(
        {.tool_calls = {TC(0, "c" + std::to_string(i), "loop_tool", "{}")},
         .finish = llm::FinishReason::kToolCalls});
  }

  MockTool loop_tool;
  loop_tool.tool_name = "loop_tool";

  EventLog log;
  eng::TurnInput input{
      .provider = &provider,
      .tools = {&loop_tool},
      .dispatch_event = log.MakeDispatcher(),
      .max_steps = 3,
  };

  auto result = eng::RunTurn(std::move(input));

  CHECK(result.stop_reason == eng::StopReason::kMaxSteps);
  CHECK(result.steps_executed == 3);
}

TEST_CASE("Loop: cancellation via stop_token") {
  std::stop_source stop_source;
  stop_source.request_stop();

  MockChatProvider provider;
  provider.responses = {
      {.text = "should not reach", .finish = llm::FinishReason::kCompleted},
  };

  EventLog log;
  eng::TurnInput input{
      .provider = &provider,
      .dispatch_event = log.MakeDispatcher(),
      .stop_token = stop_source.get_token(),
  };

  auto result = eng::RunTurn(std::move(input));

  CHECK(result.stop_reason == eng::StopReason::kAborted);
  CHECK(result.steps_executed == 0);
}

TEST_CASE("Loop: LLM error returns kError") {
  MockChatProvider provider;
  provider.responses = {};

  EventLog log;
  eng::TurnInput input{
      .provider = &provider,
      .dispatch_event = log.MakeDispatcher(),
  };

  auto result = eng::RunTurn(std::move(input));

  CHECK(result.stop_reason == eng::StopReason::kError);
  CHECK_FALSE(result.error_message.empty());
}

TEST_CASE("Loop: tool not found produces error result, loop continues") {
  MockChatProvider provider;
  provider.responses = {
      {.tool_calls = {TC(0, "c1", "nonexistent", "{}")},
       .finish = llm::FinishReason::kToolCalls},
      {.text = "Recovered", .finish = llm::FinishReason::kCompleted},
  };

  EventLog log;
  eng::TurnInput input{
      .provider = &provider,
      .dispatch_event = log.MakeDispatcher(),
  };

  auto result = eng::RunTurn(std::move(input));

  CHECK(result.stop_reason == eng::StopReason::kCompleted);
  CHECK(log.CountToolResults() == 1);

  auto tr = log.LastToolResult();
  REQUIRE(tr.has_value());
  CHECK(tr->result.is_error);
  CHECK(tr->result.content.find("not found") != std::string::npos);
}

TEST_CASE("Loop: tool execution error becomes error result in history") {
  MockChatProvider provider;
  provider.responses = {
      {.tool_calls = {TC(0, "c1", "failing_tool", "{}")},
       .finish = llm::FinishReason::kToolCalls},
      {.text = "Handled error", .finish = llm::FinishReason::kCompleted},
  };

  MockTool failing;
  failing.tool_name = "failing_tool";
  failing.handler = [](const json&) -> eng::ToolResult {
    return {.content = "permission denied", .is_error = true};
  };

  EventLog log;
  eng::TurnInput input{
      .provider = &provider,
      .tools = {&failing},
      .dispatch_event = log.MakeDispatcher(),
  };

  auto result = eng::RunTurn(std::move(input));

  CHECK(result.stop_reason == eng::StopReason::kCompleted);
  auto tr = log.LastToolResult();
  REQUIRE(tr.has_value());
  CHECK(tr->result.is_error);
  CHECK(tr->result.content == "permission denied");
}

TEST_CASE("Loop: usage accumulates across steps") {
  MockChatProvider provider;
  provider.responses = {
      {.tool_calls = {TC(0, "c1", "t", "{}")},
       .finish = llm::FinishReason::kToolCalls,
       .usage = {.input_other = 100, .output = 10}},
      {.text = "done",
       .finish = llm::FinishReason::kCompleted,
       .usage = {.input_other = 120, .output = 5}},
  };

  MockTool t;
  t.tool_name = "t";

  EventLog log;
  eng::TurnInput input{
      .provider = &provider,
      .tools = {&t},
      .dispatch_event = log.MakeDispatcher(),
  };

  auto result = eng::RunTurn(std::move(input));

  CHECK(result.total_usage.input_other == 220);
  CHECK(result.total_usage.output == 15);
}

TEST_CASE("Loop: history accumulates assistant and tool messages") {
  MockChatProvider provider;
  provider.responses = {
      {.text = "thinking",
       .tool_calls = {TC(0, "c1", "t", "{}")},
       .finish = llm::FinishReason::kToolCalls},
      {.text = "final answer", .finish = llm::FinishReason::kCompleted},
  };

  MockTool t;
  t.tool_name = "t";

  llm::Message user_msg;
  user_msg.role = llm::Role::kUser;
  user_msg.content.push_back(llm::TextPart{"initial question"});

  EventLog log;
  eng::TurnInput input{
      .provider = &provider,
      .tools = {&t},
      .history = {user_msg},
      .dispatch_event = log.MakeDispatcher(),
  };

  auto result = eng::RunTurn(std::move(input));

  REQUIRE(result.updated_history.size() == 4);
  CHECK(result.updated_history[0].role == llm::Role::kUser);
  CHECK(result.updated_history[1].role == llm::Role::kAssistant);
  CHECK(result.updated_history[2].role == llm::Role::kTool);
  CHECK(result.updated_history[3].role == llm::Role::kAssistant);

  auto& final_assistant = result.updated_history[3];
  REQUIRE_FALSE(final_assistant.content.empty());
  auto* text = std::get_if<llm::TextPart>(&final_assistant.content[0]);
  REQUIRE(text);
  CHECK(text->text == "final answer");
}

TEST_CASE("Loop: hooks are called") {
  MockChatProvider provider;
  provider.responses = {
      {.text = "hello", .finish = llm::FinishReason::kCompleted},
  };

  int before_count = 0;
  int after_count = 0;

  eng::LoopHooks hooks{
      .before_step = [&](int) { ++before_count; },
      .after_step = [&](int) { ++after_count; },
  };

  eng::TurnInput input{.provider = &provider};

  eng::RunTurn(std::move(input), hooks);

  CHECK(before_count == 1);
  CHECK(after_count == 1);
}

TEST_CASE("Loop: no tools means single-step text response") {
  MockChatProvider provider;
  provider.responses = {
      {.text = "I have no tools", .finish = llm::FinishReason::kCompleted},
  };

  EventLog log;
  eng::TurnInput input{
      .provider = &provider,
      .dispatch_event = log.MakeDispatcher(),
  };

  auto result = eng::RunTurn(std::move(input));

  CHECK(result.steps_executed == 1);
  CHECK(result.stop_reason == eng::StopReason::kCompleted);
  CHECK(log.CountAssistantDeltas() == 1);
}
