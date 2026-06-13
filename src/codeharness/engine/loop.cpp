#include "codeharness/engine/loop.h"

#include <utility>

#include "absl/status/status.h"
#include "codeharness/core/error.h"
#include "codeharness/core/log.h"
#include "codeharness/core/message.h"

namespace codeharness {
namespace engine {

namespace {

auto MapFinishReasonToStopReason(FinishReason reason) -> TurnStopReason {
  switch (reason) {
    case FinishReason::kCompleted:
      return TurnStopReason::kEndTurn;
    case FinishReason::kToolCalls:
      return TurnStopReason::kToolUse;
    case FinishReason::kTruncated:
      return TurnStopReason::kMaxTokens;
    case FinishReason::kFiltered:
      return TurnStopReason::kFiltered;
    case FinishReason::kPaused:
      return TurnStopReason::kPaused;
    default:
      return TurnStopReason::kEndTurn;
  }
}

auto AddUsage(ProviderUsage& total, const ProviderUsage& turn) -> void {
  total.input_tokens += turn.input_tokens;
  total.output_tokens += turn.output_tokens;
  total.total_tokens += turn.NormalizedTotal();
}

}  // namespace

auto RunTurn(TurnInput& input, ChatProvider& provider) -> absl::StatusOr<TurnResult> {
  int step = 0;
  TurnResult result;
  ToolCallDeduplicator deduplicator;

  while (step < input.max_steps) {
    ++step;

    // Before-step hook.
    if (input.hooks != nullptr) {
      auto hook_result = input.hooks->BeforeStep(step);
      if (!hook_result.ok()) {
        return hook_result.status();
      }
      if (hook_result->has_value() && hook_result->value().blocked) {
        result.stop_reason = TurnStopReason::kCancelled;
        break;
      }
    }

    // Check cancellation.
    if (input.cancellation.is_cancelled()) {
      result.stop_reason = TurnStopReason::kCancelled;
      break;
    }

    // Emit step started.
    if (input.event_dispatcher != nullptr) {
      input.event_dispatcher->Recorded(LoopStepStarted{step});
    }

    // Execute the provider call with retry.
    auto step_result = ChatWithRetry(provider, input.messages, *input.event_dispatcher, input.cancellation);
    if (!step_result.ok()) {
      return step_result.status();
    }

    // Accumulate usage.
    AddUsage(result.usage, step_result->usage);

    // After-step hook.
    if (input.hooks != nullptr) {
      auto after_status = input.hooks->AfterStep(step);
      if (!after_status.ok()) {
        spdlog::warn("AfterStep hook failed: {}", after_status.message());
      }
    }

    if (step_result->HasToolCalls()) {
      // Execute tool calls and inject results back into messages.
      auto tool_results = RunToolCallBatch(step_result->tool_calls, input.tools, deduplicator, *input.event_dispatcher,
                                           input.hooks, input.cancellation);

      // Build a tool-result message.
      Message tool_message;
      tool_message.role = Role::kTool;
      for (auto& tr : tool_results) {
        tool_message.content.emplace_back(std::move(tr));
      }

      // Update input.messages: append assistant response + tool results.
      // The assistant response is re-constructed from the step result.
      Message assistant_message;
      assistant_message.role = Role::kAssistant;
      if (!step_result->text.empty()) {
        assistant_message.content.emplace_back(TextBlock{step_result->text});
      }
      for (const auto& tc : step_result->tool_calls) {
        assistant_message.content.emplace_back(tc);
      }

      input.messages.push_back(std::move(assistant_message));
      input.messages.push_back(std::move(tool_message));

      // Emit step ended.
      if (input.event_dispatcher != nullptr) {
        input.event_dispatcher->Recorded(LoopStepEnded{step, TurnStopReason::kToolUse});
      }

      continue;
    }

    // No tool calls -- terminal stop reason.
    auto stop_reason = MapFinishReasonToStopReason(step_result->finish_reason);

    // Append assistant message.
    Message assistant_message;
    assistant_message.role = Role::kAssistant;
    if (!step_result->text.empty()) {
      assistant_message.content.emplace_back(TextBlock{std::move(step_result->text)});
    }
    input.messages.push_back(std::move(assistant_message));

    // Emit step ended.
    if (input.event_dispatcher != nullptr) {
      input.event_dispatcher->Recorded(LoopStepEnded{step, stop_reason});
    }

    // Handle max_tokens with hook check.
    if (stop_reason == TurnStopReason::kMaxTokens && input.hooks != nullptr) {
      auto should_continue = input.hooks->ShouldContinueAfterStop(stop_reason);
      if (should_continue.ok() && *should_continue) {
        continue;
      }
    }

    result.stop_reason = stop_reason;
    result.steps = step;
    return result;
  }

  // Max steps exceeded.
  if (step >= input.max_steps) {
    result.stop_reason = TurnStopReason::kMaxSteps;
    result.steps = step;
  }

  return result;
}

}  // namespace engine
}  // namespace codeharness
