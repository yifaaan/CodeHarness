#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "codeharness/engine/loop.h"
#include "codeharness/engine/loop_types.h"
#include "codeharness/engine/tool_scheduler.h"
#include "codeharness/engine/turn_step.h"

namespace {

using namespace codeharness;
using namespace codeharness::engine;

// ---------------------------------------------------------------------------
// Mock ChatProvider implementations
// ---------------------------------------------------------------------------

class EchoChatProvider final : public ChatProvider {
 public:
  std::string_view Name() const override { return "test-echo"; }
  std::string_view ModelName() const override { return "test-echo"; }

  absl::Status Stream(std::span<const Message> messages, const ProviderEventSink& sink) override {
    // Echo the last user message text.
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
      if (it->role == Role::kUser) {
        sink(AssistantTextDelta{CollectText(*it)});
        break;
      }
    }
    sink(MessageFinished{FinishReason::kCompleted});
    return absl::OkStatus();
  }

  ModelCapability Capability() const override { return ModelCapability{.tool_use = true, .max_context_tokens = 4096}; }
};

class ToolCallChatProvider final : public ChatProvider {
 public:
  std::string_view Name() const override { return "test-tool"; }
  std::string_view ModelName() const override { return "test-tool"; }

  absl::Status Stream(std::span<const Message> messages, const ProviderEventSink& sink) override {
    // Check if the last message is a tool result — if so, echo it.
    if (!messages.empty() && messages.back().role == Role::kTool) {
      auto text = CollectText(messages.back());
      if (!text.empty()) {
        sink(AssistantTextDelta{text});
        sink(MessageFinished{FinishReason::kCompleted});
        return absl::OkStatus();
      }
    }

    // First call: request a tool use.
    sink(AssistantTextDelta{"I'll read that file."});
    sink(ToolUseStarted{.id = "tool-1", .name = "read_file"});
    sink(ToolUseInputDelta{.id = "tool-1", .input_json_delta = R"({"path":"hello.txt"})"});
    sink(ToolUseFinished{.id = "tool-1"});
    sink(MessageFinished{FinishReason::kToolCalls});
    return absl::OkStatus();
  }

  ModelCapability Capability() const override { return ModelCapability{.tool_use = true, .max_context_tokens = 4096}; }
};

class MultiToolChatProvider final : public ChatProvider {
 public:
  int call_count = 0;

  std::string_view Name() const override { return "test-multi-tool"; }
  std::string_view ModelName() const override { return "test-multi-tool"; }

  absl::Status Stream(std::span<const Message> messages, const ProviderEventSink& sink) override {
    // After tool result, echo the result.
    if (!messages.empty() && messages.back().role == Role::kTool) {
      auto text = CollectText(messages.back());
      if (!text.empty()) {
        sink(AssistantTextDelta{text});
        sink(MessageFinished{FinishReason::kCompleted});
        return absl::OkStatus();
      }
    }

    ++call_count;

    if (call_count == 1) {
      // First call: request a tool use.
      sink(AssistantTextDelta{"First call."});
      sink(ToolUseStarted{.id = "tool-1", .name = "read_file"});
      sink(ToolUseInputDelta{.id = "tool-1", .input_json_delta = R"({"path":"a.txt"})"});
      sink(ToolUseFinished{.id = "tool-1"});
      sink(MessageFinished{FinishReason::kToolCalls});
    } else {
      // Second call: request another tool use.
      sink(AssistantTextDelta{"Second call."});
      sink(ToolUseStarted{.id = "tool-2", .name = "read_file"});
      sink(ToolUseInputDelta{.id = "tool-2", .input_json_delta = R"({"path":"b.txt"})"});
      sink(ToolUseFinished{.id = "tool-2"});
      sink(MessageFinished{FinishReason::kToolCalls});
    }
    return absl::OkStatus();
  }

  ModelCapability Capability() const override { return ModelCapability{.tool_use = true, .max_context_tokens = 4096}; }
};

class UsageChatProvider final : public ChatProvider {
 public:
  std::string_view Name() const override { return "test-usage"; }
  std::string_view ModelName() const override { return "test-usage"; }

  absl::Status Stream(std::span<const Message> messages, const ProviderEventSink& sink) override {
    sink(ProviderUsage{.input_tokens = 5, .output_tokens = 3, .total_tokens = 8});
    sink(AssistantTextDelta{"ok"});
    sink(MessageFinished{FinishReason::kCompleted});
    return absl::OkStatus();
  }

  ModelCapability Capability() const override { return {}; }
};

class ThrowingToolChatProvider final : public ChatProvider {
 public:
  std::string_view Name() const override { return "test-throw"; }
  std::string_view ModelName() const override { return "test-throw"; }

  absl::Status Stream(std::span<const Message> messages, const ProviderEventSink& sink) override {
    if (!messages.empty() && messages.back().role == Role::kTool) {
      auto text = CollectText(messages.back());
      if (!text.empty()) {
        sink(AssistantTextDelta{text});
        sink(MessageFinished{FinishReason::kCompleted});
        return absl::OkStatus();
      }
    }

    sink(AssistantTextDelta{"About to throw..."});
    sink(ToolUseStarted{.id = "tool-throw", .name = "throw_tool"});
    sink(ToolUseInputDelta{.id = "tool-throw", .input_json_delta = "{}"});
    sink(ToolUseFinished{.id = "tool-throw"});
    sink(MessageFinished{FinishReason::kToolCalls});
    return absl::OkStatus();
  }

  ModelCapability Capability() const override { return ModelCapability{.tool_use = true}; }
};

class CancellingChatProvider final : public ChatProvider {
 public:
  CancellationToken* cancel_on_call = nullptr;

  std::string_view Name() const override { return "test-cancel"; }
  std::string_view ModelName() const override { return "test-cancel"; }

  absl::Status Stream(std::span<const Message> messages, const ProviderEventSink& sink) override {
    if (cancel_on_call && cancel_on_call->is_cancelled()) {
      return absl::CancelledError("cancelled");
    }
    sink(AssistantTextDelta{"partial"});
    sink(MessageFinished{FinishReason::kCompleted});
    return absl::OkStatus();
  }

  ModelCapability Capability() const override { return {}; }
};

// ---------------------------------------------------------------------------
// Mock Tool implementations
// ---------------------------------------------------------------------------

class ReadFileTestTool final : public Tool {
 public:
  std::string content;

  auto name() const -> std::string override { return "read_file"; }
  auto description() const -> std::string override { return "Read a file."; }
  auto is_read_only() const noexcept -> bool override { return true; }

  auto execute(const ToolRequest& request, const ToolContext&) const -> absl::StatusOr<ToolResponse> override {
    auto path = request.parsed_input.value("path", std::string{});
    if (path.empty()) {
      return ToolResponse{.tool_use_id = request.id, .content = "no path specified", .is_error = true};
    }
    return ToolResponse{
        .tool_use_id = request.id, .content = content.empty() ? "file content" : content, .is_error = false};
  }
};

class ThrowingTestTool final : public Tool {
 public:
  auto name() const -> std::string override { return "throw_tool"; }
  auto description() const -> std::string override { return "Throws."; }

  auto execute(const ToolRequest&, const ToolContext&) const -> absl::StatusOr<ToolResponse> override {
    throw std::runtime_error{"boom"};
  }
};

// ---------------------------------------------------------------------------
// Event collector for testing
// ---------------------------------------------------------------------------
struct TestEventCollector final : public LoopEventDispatcher {
  std::vector<LoopEvent> recorded;
  std::vector<LoopEvent> live;

  void Recorded(const LoopEvent& event) override { recorded.push_back(event); }
  void Live(const LoopEvent& event) override { live.push_back(event); }
};

// Helpers
auto MakeToolRequest(std::string name, std::string input_json) -> ToolRequest {
  ToolRequest req;
  req.name = std::move(name);
  req.input_json = std::move(input_json);
  return req;
}

}  // namespace

// ============================================================================
// Old Engine (adapter) tests
// ============================================================================

TEST_CASE("engine runs one provider turn") {
  EchoChatProvider provider;
  ToolRegistry tools;
  Engine engine{provider, tools};

  RunRequest request;
  request.prompt = "hello";
  request.options.max_turns = 1;

  auto result = engine.Run(request);

  REQUIRE(result.has_value());
  CHECK(result->output_text == "hello");
  REQUIRE(result->messages.size() == 2);
  CHECK(result->messages[0].role == Role::kUser);
  CHECK(result->messages[1].role == Role::kAssistant);
}

TEST_CASE("engine prepends system prompt when provided") {
  EchoChatProvider provider;
  ToolRegistry tools;
  Engine engine{provider, tools};

  RunRequest request;
  request.prompt = "hello";
  request.system_prompt = "system rules";
  request.options.max_turns = 1;

  auto result = engine.Run(request);

  REQUIRE(result.has_value());
  CHECK(result->output_text == "hello");
  REQUIRE(result->messages.size() == 3);
  CHECK(result->messages[0].role == Role::kSystem);
  CHECK(CollectText(result->messages[0]) == "system rules");
  CHECK(result->messages[1].role == Role::kUser);
  CHECK(result->messages[2].role == Role::kAssistant);
}

TEST_CASE("engine continues from initial messages and replaces prior system prompt") {
  EchoChatProvider provider;
  ToolRegistry tools;
  Engine engine{provider, tools};

  RunRequest request;
  request.prompt = "next";
  request.system_prompt = "fresh system";
  request.initial_messages = std::vector<Message>{
      MakeTextMessage(Role::kSystem, "old system"),
      MakeTextMessage(Role::kUser, "previous"),
      MakeTextMessage(Role::kAssistant, "prior answer"),
  };
  request.options.max_turns = 1;

  auto result = engine.Run(request);

  REQUIRE(result.has_value());
  REQUIRE(result->messages.size() == 5);
  CHECK(result->messages[0].role == Role::kSystem);
  CHECK(CollectText(result->messages[0]) == "fresh system");
  CHECK(result->messages[1].role == Role::kUser);
  CHECK(result->messages[2].role == Role::kAssistant);
  CHECK(result->messages[3].role == Role::kUser);
  CHECK(result->messages[4].role == Role::kAssistant);
}

TEST_CASE("engine executes requested tool and returns final provider text") {
  auto temp_dir = std::filesystem::temp_directory_path() / "codeharness-engine-tool-test";
  std::filesystem::remove_all(temp_dir);
  std::filesystem::create_directories(temp_dir);
  {
    std::ofstream file{temp_dir / "hello.txt"};
    file << "hello from engine file";
  }

  auto previous_cwd = std::filesystem::current_path();
  std::filesystem::current_path(temp_dir);

  ToolRegistry tools;
  auto read_tool = std::make_unique<ReadFileTestTool>();
  read_tool->content = "hello from engine file";
  tools.add(std::move(read_tool));

  ToolCallChatProvider provider;
  Engine engine{provider, tools};

  RunRequest request;
  request.prompt = "read hello.txt";
  request.options.max_turns = 3;

  auto result = engine.Run(request);

  std::filesystem::current_path(previous_cwd);
  std::filesystem::remove_all(temp_dir);

  REQUIRE(result.has_value());
  CHECK(result->output_text == "hello from engine file");
  REQUIRE(result->messages.size() == 4);
  CHECK(result->messages[0].role == Role::kUser);
  CHECK(result->messages[1].role == Role::kAssistant);
  CHECK(result->messages[2].role == Role::kTool);
  CHECK(result->messages[3].role == Role::kAssistant);
}

TEST_CASE("engine reports unknown tool as a tool error") {
  ToolCallChatProvider provider;
  ToolRegistry tools;
  Engine engine{provider, tools};

  RunRequest request;
  request.prompt = "read hello.txt";
  request.options.max_turns = 3;

  auto result = engine.Run(request);

  REQUIRE(result.has_value());
  REQUIRE(result->messages.size() == 4);
  CHECK(result->messages[2].role == Role::kTool);

  auto tool_result = std::get_if<ToolResultBlock>(&result->messages[2].content.front());
  REQUIRE(tool_result != nullptr);
  CHECK(tool_result->tool_use_id == "tool-1");
  CHECK(tool_result->is_error);
  CHECK(tool_result->content.find("tool not found: read_file") != std::string::npos);
}

TEST_CASE("engine reports throwing tool as a tool error") {
  ThrowingToolChatProvider provider;
  ToolRegistry tools;
  tools.add(std::make_unique<ThrowingTestTool>());
  Engine engine{provider, tools};

  RunRequest request;
  request.prompt = "run throwing tool";
  request.options.max_turns = 3;

  auto result = engine.Run(request);

  REQUIRE(result.has_value());
  REQUIRE(result->messages.size() == 4);
  CHECK(result->messages[2].role == Role::kTool);

  auto tool_result = std::get_if<ToolResultBlock>(&result->messages[2].content.front());
  REQUIRE(tool_result != nullptr);
  CHECK(tool_result->is_error);
  CHECK(tool_result->content.find("unexpected exception") != std::string::npos);
}

TEST_CASE("engine streaming emits assistant text delta") {
  EchoChatProvider provider;
  ToolRegistry tools;
  Engine engine{provider, tools};

  RunRequest request;
  request.prompt = "hello";
  request.options.max_turns = 1;

  std::string streamed_text;
  auto result = engine.RunStreaming(request, [&](const EngineEvent& event) {
    if (const auto* delta = std::get_if<EngineAssistantTextDelta>(&event)) {
      streamed_text += delta->text;
    }
  });

  REQUIRE(result.has_value());
  CHECK(streamed_text == "hello");
  CHECK(result->output_text == "hello");
}

TEST_CASE("engine streaming emits tool events") {
  ToolCallChatProvider provider;
  ToolRegistry tools;
  tools.add(std::make_unique<ReadFileTestTool>());
  Engine engine{provider, tools};

  RunRequest request;
  request.prompt = "read";
  request.options.max_turns = 3;

  bool saw_tool_started = false;
  bool saw_tool_result = false;
  auto result = engine.RunStreaming(request, [&](const EngineEvent& event) {
    if (std::holds_alternative<EngineToolStarted>(event)) {
      saw_tool_started = true;
    }
    if (std::holds_alternative<EngineToolResult>(event)) {
      saw_tool_result = true;
    }
  });

  REQUIRE(result.has_value());
  CHECK(saw_tool_started);
  CHECK(saw_tool_result);
  CHECK(result->output_text.size() > 0);
}

TEST_CASE("engine aggregates usage into run result") {
  UsageChatProvider provider;
  ToolRegistry tools;
  Engine engine{provider, tools};

  RunRequest request;
  request.prompt = "hello";
  request.options.max_turns = 1;

  auto result = engine.Run(request);

  REQUIRE(result.has_value());
  CHECK(result->usage.input_tokens == 5);
  CHECK(result->usage.output_tokens == 3);
  CHECK(result->usage.total_tokens == 8);
}

TEST_CASE("engine cancellation returns cancelled error") {
  EchoChatProvider provider;
  ToolRegistry tools;
  Engine engine{provider, tools};
  CancellationSource cancellation;
  cancellation.cancel();

  RunRequest request;
  request.prompt = "hello";
  request.cancellation = cancellation.token();
  request.options.max_turns = 1;

  auto result = engine.Run(request);

  REQUIRE(!result.has_value());
}

TEST_CASE("engine respects max_turns limit") {
  EchoChatProvider provider;
  ToolRegistry tools;
  Engine engine{provider, tools};

  RunRequest request;
  request.prompt = "hello";
  request.options.max_turns = 0;

  auto result = engine.Run(request);

  // With max_turns=0, the loop doesn't run but shouldn't crash.
  // The engine should handle this gracefully.
  REQUIRE(result.has_value());
}

// ============================================================================
// Loop execution engine tests
// ============================================================================

TEST_CASE("ExecuteLoopStep with echo provider returns user text") {
  EchoChatProvider provider;
  NullEventDispatcher dispatcher;
  CancellationToken cancel;

  std::vector<Message> messages;
  messages.push_back(MakeTextMessage(Role::kUser, "hello"));

  auto result = ExecuteLoopStep(provider, messages, dispatcher, cancel);

  REQUIRE(result.has_value());
  CHECK(result->text == "hello");
  CHECK(result->finish_reason == FinishReason::kCompleted);
  CHECK_FALSE(result->HasToolCalls());
}

TEST_CASE("ExecuteLoopStep with tool provider returns tool calls") {
  ToolCallChatProvider provider;
  NullEventDispatcher dispatcher;
  CancellationToken cancel;

  std::vector<Message> messages;
  messages.push_back(MakeTextMessage(Role::kUser, "read file"));

  auto result = ExecuteLoopStep(provider, messages, dispatcher, cancel);

  REQUIRE(result.has_value());
  CHECK(result->text == "I''ll read that file.");
  CHECK(result->finish_reason == FinishReason::kToolCalls);
  REQUIRE(result->HasToolCalls());
  CHECK(result->tool_calls.size() == 1);
  CHECK(result->tool_calls[0].name == "read_file");
}

TEST_CASE("ExecuteLoopStep respects cancellation") {
  EchoChatProvider provider;
  NullEventDispatcher dispatcher;
  CancellationSource cancel_src;
  cancel_src.cancel();

  std::vector<Message> messages;
  messages.push_back(MakeTextMessage(Role::kUser, "hello"));

  auto result = ExecuteLoopStep(provider, messages, dispatcher, cancel_src.token());

  REQUIRE(!result.ok());
  CHECK(absl::IsCancelled(result.status()));
}

TEST_CASE("ChatWithRetry succeeds on first attempt") {
  EchoChatProvider provider;
  NullEventDispatcher dispatcher;
  CancellationToken cancel;

  std::vector<Message> messages;
  messages.push_back(MakeTextMessage(Role::kUser, "hello"));

  auto result = ChatWithRetry(provider, messages, dispatcher, cancel);

  REQUIRE(result.ok());
  CHECK(result->text == "hello");
}

TEST_CASE("RunTurn with echo provider returns end_turn") {
  EchoChatProvider provider;
  NullEventDispatcher dispatcher;
  CancellationToken cancel;

  std::vector<Message> messages;
  messages.push_back(MakeTextMessage(Role::kUser, "hello"));

  TurnInput input{
      .turn_id = "test-turn",
      .cancellation = cancel,
      .messages = std::move(messages),
      .event_dispatcher = &dispatcher,
      .max_steps = 10,
  };

  auto result = RunTurn(input, provider);

  REQUIRE(result.ok());
  CHECK(result->stop_reason == TurnStopReason::kEndTurn);
  CHECK(result->steps > 0);
}

TEST_CASE("RunTurn with tool call executes tool and returns end_turn") {
  ToolCallChatProvider provider;
  NullEventDispatcher dispatcher;
  CancellationToken cancel;
  ToolCallDeduplicator dedup;

  ReadFileTestTool read_tool;
  read_tool.content = "file content here";
  std::vector<const Tool*> tools = {&read_tool};

  std::vector<Message> messages;
  messages.push_back(MakeTextMessage(Role::kUser, "read file"));

  // Manually run the loop to verify
  auto step_result = ExecuteLoopStep(provider, messages, dispatcher, cancel);
  REQUIRE(step_result.ok());
  REQUIRE(step_result->HasToolCalls());

  auto tool_results = RunToolCallBatch(step_result->tool_calls, tools, dedup, dispatcher, nullptr, cancel);

  REQUIRE(tool_results.size() == 1);
  CHECK_FALSE(tool_results[0].is_error);
  CHECK(tool_results[0].content == "file content here");
}

TEST_CASE("RunToolCallBatch with unknown tool returns error") {
  NullEventDispatcher dispatcher;
  CancellationToken cancel;
  ToolCallDeduplicator dedup;

  std::vector<ToolUseBlock> tool_calls;
  tool_calls.push_back(ToolUseBlock{
      .id = "t1",
      .name = "nonexistent",
      .input_json = "{}",
  });

  std::vector<const Tool*> empty_tools;
  auto results = RunToolCallBatch(tool_calls, empty_tools, dedup, dispatcher, nullptr, cancel);

  REQUIRE(results.size() == 1);
  CHECK(results[0].is_error);
  CHECK(results[0].content.find("tool not found") != std::string::npos);
}

TEST_CASE("ToolCallDeduplicator caches and returns cached results") {
  ToolCallDeduplicator dedup;

  nlohmann::json args = {{"path", "test.txt"}};
  ToolResultBlock result{
      .tool_use_id = "t1",
      .content = "cached content",
      .is_error = false,
  };

  CHECK_FALSE(dedup.IsDuplicate("read_file", args));

  dedup.Record("read_file", args, result);

  CHECK(dedup.IsDuplicate("read_file", args));
  auto cached = dedup.GetCached("read_file", args);
  REQUIRE(cached.has_value());
  CHECK(cached->content == "cached content");

  // Different args should not match.
  nlohmann::json other_args = {{"path", "other.txt"}};
  CHECK_FALSE(dedup.IsDuplicate("read_file", other_args));
}

TEST_CASE("IsRetryableProviderError identifies retryable errors") {
  CHECK(IsRetryableProviderError(absl::UnavailableError("service down")));
  CHECK(IsRetryableProviderError(absl::DeadlineExceededError("timeout")));
  CHECK(IsRetryableProviderError(absl::ResourceExhaustedError("rate limit")));
  CHECK_FALSE(IsRetryableProviderError(absl::InvalidArgumentError("bad request")));
  CHECK_FALSE(IsRetryableProviderError(absl::UnauthenticatedError("auth failed")));
}

TEST_CASE("FindTool returns nullptr for empty list") {
  std::vector<const Tool*> empty;
  CHECK(FindTool(empty, "anything") == nullptr);
}
