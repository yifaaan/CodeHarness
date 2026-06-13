#include "loop.h"

#include <algorithm>
#include <span>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "fmt/format.h"
#include "spdlog/spdlog.h"

#include "llm/chat_provider.h"

namespace codeharness::engine {

namespace {

void Dispatch(const TurnInput& input, LoopEvent event) {
  if (input.dispatch_event) {
    input.dispatch_event(event);
  }
}

ToolResult ExecuteToolCall(ExecutableTool& tool, const llm::ToolCall& tc,
                           const ToolContext& ctx) {
  nlohmann::json args;
  if (!tc.arguments.empty()) {
    try {
      args = nlohmann::json::parse(tc.arguments);
    } catch (const nlohmann::json::parse_error& e) {
      return {.content = fmt::format("invalid tool arguments: {}", e.what()), .is_error = true};
    }
  }

  auto resolution = tool.ResolveExecution(args);
  if (!resolution.ok()) {
    return {.content = std::string(resolution.status().message()), .is_error = true};
  }

  auto result = tool.Execute(args, ctx);
  if (!result.ok()) {
    return {.content = std::string(result.status().message()), .is_error = true};
  }

  return std::move(*result);
}

ExecutableTool* FindTool(std::vector<ExecutableTool*>& tools, std::string_view name) {
  for (auto* t : tools) {
    if (t->Name() == name) return t;
  }
  return nullptr;
}

}  // namespace

TurnResult RunTurn(TurnInput input, const LoopHooks& hooks) {
  TurnResult result;
  result.updated_history = std::move(input.history);

  std::vector<llm::Tool> tool_defs;
  tool_defs.reserve(input.tools.size());
  for (auto* t : input.tools) {
    tool_defs.push_back(t->GetToolDefinition());
  }

  for (int step = 1; step <= input.max_steps; ++step) {
    if (input.stop_token.stop_requested()) {
      result.stop_reason = StopReason::kAborted;
      return result;
    }

    if (hooks.before_step) hooks.before_step(step);

    Dispatch(input, StepStartedEvent{step});

    std::string assistant_text;
    std::vector<llm::ToolCall> pending_calls;
    llm::FinishReason finish_reason = llm::FinishReason::kOther;
    llm::TokenUsage step_usage{};

    llm::StreamCallbacks callbacks{
        .on_text =
            [&](std::string_view text) {
              assistant_text += text;
              Dispatch(input, AssistantDeltaEvent{std::string(text)});
            },
        .on_think = {},
        .on_tool_call_start =
            [&](int idx, std::string_view id, std::string_view name) {
              if (idx >= static_cast<int>(pending_calls.size())) {
                pending_calls.resize(idx + 1);
              }
              pending_calls[idx].id = std::string(id);
              pending_calls[idx].name = std::string(name);
            },
        .on_tool_call_delta =
            [&](int idx, std::string_view args) {
              if (idx < static_cast<int>(pending_calls.size())) {
                pending_calls[idx].arguments += args;
              }
            },
        .on_finish =
            [&](llm::FinishReason f, const llm::TokenUsage& u) {
              finish_reason = f;
              step_usage = u;
            },
    };

    auto status = input.provider->Generate(
        input.system_prompt, std::span<const llm::Tool>(tool_defs),
        std::span<const llm::Message>(result.updated_history), callbacks, input.stop_token);

    if (!status.ok()) {
      result.stop_reason = StopReason::kError;
      result.error_message = std::string(status.message());
      Dispatch(input, ErrorEvent{result.error_message});
      return result;
    }

    result.total_usage.output += step_usage.output;
    result.total_usage.input_other += step_usage.input_other;
    result.total_usage.input_cache_read += step_usage.input_cache_read;
    result.total_usage.input_cache_creation += step_usage.input_cache_creation;

    pending_calls.erase(std::remove_if(pending_calls.begin(), pending_calls.end(),
                                        [](const llm::ToolCall& tc) { return tc.name.empty(); }),
                         pending_calls.end());

    llm::Message assistant_msg;
    assistant_msg.role = llm::Role::kAssistant;
    if (!assistant_text.empty()) {
      assistant_msg.content.push_back(llm::TextPart{std::move(assistant_text)});
    }
    assistant_msg.tool_calls = pending_calls;
    result.updated_history.push_back(std::move(assistant_msg));

    result.steps_executed = step;

    Dispatch(input, StepCompletedEvent{step});
    if (hooks.after_step) hooks.after_step(step);

    bool has_tool_calls = !pending_calls.empty();
    if (finish_reason != llm::FinishReason::kToolCalls || !has_tool_calls) {
      if (hooks.should_continue_after_stop) {
        std::string reason_str = finish_reason == llm::FinishReason::kTruncated ? "max_tokens"
                                  : finish_reason == llm::FinishReason::kCompleted
                                      ? "end_turn"
                                      : "other";
        if (hooks.should_continue_after_stop(reason_str)) {
          continue;
        }
      }
      result.stop_reason = StopReason::kCompleted;
      return result;
    }

    ToolContext ctx{.stop_token = input.stop_token};

    for (const auto& tc : pending_calls) {
      if (input.stop_token.stop_requested()) {
        result.stop_reason = StopReason::kAborted;
        return result;
      }

      nlohmann::json args;
      if (!tc.arguments.empty()) {
        try {
          args = nlohmann::json::parse(tc.arguments);
        } catch (...) {
          args = nlohmann::json::object();
        }
      }

      Dispatch(input, ToolCallStartedEvent{tc.id, tc.name, args});

      auto* tool = FindTool(input.tools, tc.name);
      ToolResult tool_result;
      if (!tool) {
        tool_result.is_error = true;
        tool_result.content = fmt::format("tool '{}' not found", tc.name);
        spdlog::warn("tool not found: {}", tc.name);
      } else {
        tool_result = ExecuteToolCall(*tool, tc, ctx);
      }

      Dispatch(input, ToolResultEvent{tc.id, tc.name, tool_result});

      llm::Message tool_msg;
      tool_msg.role = llm::Role::kTool;
      tool_msg.tool_call_id = tc.id;
      tool_msg.content.push_back(llm::TextPart{tool_result.content});
      result.updated_history.push_back(std::move(tool_msg));
    }
  }

  result.stop_reason = StopReason::kMaxSteps;
  return result;
}

}  // namespace codeharness::engine
