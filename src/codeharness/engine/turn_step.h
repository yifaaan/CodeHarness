#pragma once

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "codeharness/core/cancellation.h"
#include "codeharness/core/message.h"
#include "codeharness/engine/loop_types.h"
#include "codeharness/provider/provider.h"

namespace codeharness {
namespace engine {

// ---------------------------------------------------------------------------
// ExecuteStepResult -- returned by ExecuteLoopStep()
// ---------------------------------------------------------------------------
struct ExecuteStepResult {
  std::vector<ToolUseBlock> tool_calls;
  std::string text;
  FinishReason finish_reason = FinishReason::kUnknown;
  ProviderUsage usage;

  [[nodiscard]] auto HasToolCalls() const noexcept -> bool { return !tool_calls.empty(); }
};

// ---------------------------------------------------------------------------
// Execute a single step in the loop:
//   1. Call ChatProvider::Stream() with tool schemas
//   2. Accumulate text + tool calls from streaming events
//   3. Return structured result
//
// The provider must already be configured with tool schemas at construction.
// ---------------------------------------------------------------------------
absl::StatusOr<ExecuteStepResult> ExecuteLoopStep(ChatProvider& provider, const std::vector<Message>& messages,
                                                  LoopEventDispatcher& event_dispatcher,
                                                  const CancellationToken& cancellation);

// ---------------------------------------------------------------------------
// ChatWithRetry -- wraps ExecuteLoopStep with exponential backoff retry
//
// Retries on transient errors (rate limits, network issues).
// Non-retryable errors (auth, invalid request) are thrown immediately.
// ---------------------------------------------------------------------------
absl::StatusOr<ExecuteStepResult> ChatWithRetry(ChatProvider& provider, const std::vector<Message>& messages,
                                                LoopEventDispatcher& event_dispatcher,
                                                const CancellationToken& cancellation, int max_attempts = 3);

// ---------------------------------------------------------------------------
// IsRetryableProviderError -- heuristic for retryable provider errors
// ---------------------------------------------------------------------------
bool IsRetryableProviderError(const absl::Status& status);

}  // namespace engine
}  // namespace codeharness
