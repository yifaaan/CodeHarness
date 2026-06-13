#pragma once

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "codeharness/engine/loop_types.h"
#include "codeharness/engine/tool_scheduler.h"
#include "codeharness/engine/turn_step.h"
#include "codeharness/provider/provider.h"

namespace codeharness {
namespace engine {

// ---------------------------------------------------------------------------
// RunTurn -- stateless turn execution loop
//
// Orchestrates the LLM-tool interaction cycle:
//   repeat:
//     ExecuteLoopStep (LLM call) -> tool calls?
//       YES -> RunToolCallBatch -> inject results -> continue
//       NO  -> stop (end_turn / max_tokens / filtered / paused)
//
// Returns TurnResult with stop reason, aggregated usage, and step count.
// ---------------------------------------------------------------------------
absl::StatusOr<TurnResult> RunTurn(TurnInput& input, ChatProvider& provider);

}  // namespace engine
}  // namespace codeharness
