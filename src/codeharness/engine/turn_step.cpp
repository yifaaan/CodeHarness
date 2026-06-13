#include "codeharness/engine/turn_step.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <thread>
#include <utility>

#include "absl/status/status.h"
#include "codeharness/core/event_collector.h"
#include "codeharness/core/overloaded.h"

namespace codeharness {
namespace engine {

namespace {

// Translate a ProviderEvent into a LoopEvent for the dispatcher.
auto TranslateProviderEvent(const ProviderEvent& event) -> std::optional<LoopEvent> {
  return std::visit(
      Overloaded{
          [](const AssistantTextDelta& delta) -> std::optional<LoopEvent> { return LoopAssistantDelta{delta.text}; },
          [](const ThinkingDelta& delta) -> std::optional<LoopEvent> { return LoopThinkingDelta{delta.text}; },
          [](const ToolUseStarted& started) -> std::optional<LoopEvent> {
            return LoopToolCallStarted{.id = started.id, .name = started.name};
          },
          [](const ToolUseInputDelta& delta) -> std::optional<LoopEvent> {
            return LoopToolCallDelta{.id = delta.id, .input_json_delta = delta.input_json_delta};
          },
          [](const ToolUseFinished& finished) -> std::optional<LoopEvent> {
            return LoopToolCallFinished{.id = finished.id};
          },
          [](const MessageFinished&) -> std::optional<LoopEvent> { return std::nullopt; },
          [](const ProviderUsage&) -> std::optional<LoopEvent> { return std::nullopt; },
      },
      event);
}

}  // namespace

auto ExecuteLoopStep(ChatProvider& provider, const std::vector<Message>& messages,
                     LoopEventDispatcher& event_dispatcher, const CancellationToken& cancellation)
    -> absl::StatusOr<ExecuteStepResult> {
  if (cancellation.is_cancelled()) {
    return absl::CancelledError("execute step cancelled");
  }

  ExecuteStepResult result;
  ProviderEventCollector collector;
  collector.Message().role = Role::kAssistant;

  auto stream_status = provider.Stream(messages, [&](const ProviderEvent& event) {
    if (cancellation.is_cancelled()) {
      return;
    }

    // Accumulate into the structured message via collector.
    collector.OnEvent(event);

    // Track usage and finish reason.
    if (const auto* usage = std::get_if<ProviderUsage>(&event)) {
      result.usage = *usage;
      result.usage.total_tokens = result.usage.NormalizedTotal();
    }
    if (const auto* finished = std::get_if<MessageFinished>(&event)) {
      result.finish_reason = finished->reason;
    }

    // Forward as loop event.
    if (auto loop_event = TranslateProviderEvent(event)) {
      event_dispatcher.Live(*loop_event);
    }
  });

  if (!stream_status.ok()) {
    return stream_status;
  }
  if (cancellation.is_cancelled()) {
    event_dispatcher.Live(LoopAssistantDelta{.text = "\n\n[interrupted]"});
    return absl::CancelledError("interrupted");
  }

  auto final_message = collector.Finalize();
  if (!final_message.ok()) {
    return final_message.status();
  }

  // Extract text and tool calls from the completed message.
  for (const auto& block : final_message->content) {
    if (const auto* text = std::get_if<TextBlock>(&block)) {
      result.text += text->text;
    } else if (const auto* tool_use = std::get_if<ToolUseBlock>(&block)) {
      result.tool_calls.push_back(*tool_use);
    }
  }

  return result;
}

auto ChatWithRetry(ChatProvider& provider, const std::vector<Message>& messages, LoopEventDispatcher& event_dispatcher,
                   const CancellationToken& cancellation, int max_attempts) -> absl::StatusOr<ExecuteStepResult> {
  constexpr std::chrono::milliseconds kBaseDelay{1000};

  for (int attempt = 1; attempt <= max_attempts; ++attempt) {
    if (cancellation.is_cancelled()) {
      return absl::CancelledError("chat cancelled");
    }

    auto result = ExecuteLoopStep(provider, messages, event_dispatcher, cancellation);
    if (result.ok()) {
      return result;
    }

    if (!IsRetryableProviderError(result.status())) {
      return result;
    }

    if (attempt == max_attempts) {
      return result;
    }

    // Exponential backoff with jitter.
    auto delay = kBaseDelay * (1 << (attempt - 1));
    auto jitter = std::chrono::milliseconds{static_cast<int>(delay.count() * 0.1)};
    std::this_thread::sleep_for(delay + jitter);
  }

  return absl::UnavailableError("chat retry exhausted");
}

auto IsRetryableProviderError(const absl::Status& status) -> bool {
  // Retry on unavailable / deadline exceeded (transient).
  if (absl::IsUnavailable(status) || absl::IsDeadlineExceeded(status)) {
    return true;
  }
  // Retry on resource exhausted (rate limiting).
  if (absl::IsResourceExhausted(status)) {
    return true;
  }
  return false;
}

}  // namespace engine
}  // namespace codeharness
