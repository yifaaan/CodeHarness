#pragma once

#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "codeharness/core/cancellation.h"
#include "codeharness/core/message.h"
#include "codeharness/provider/provider.h"
#include "codeharness/tools/tool.h"

namespace codeharness {
namespace engine {

// ---------------------------------------------------------------------------
// TurnStopReason -- why a turn ended
// ---------------------------------------------------------------------------
enum class TurnStopReason {
  kEndTurn,
  kToolUse,
  kMaxTokens,
  kFiltered,
  kPaused,
  kMaxSteps,
  kCancelled,
};

// ---------------------------------------------------------------------------
// TurnResult -- returned by RunTurn()
// ---------------------------------------------------------------------------
struct TurnResult {
  TurnStopReason stop_reason = TurnStopReason::kEndTurn;
  ProviderUsage usage;
  int steps = 0;
};

// ---------------------------------------------------------------------------
// HookAction -- block or allow from lifecycle hooks
// ---------------------------------------------------------------------------
struct HookAction {
  bool blocked = false;
  std::string reason;
};

// ---------------------------------------------------------------------------
// LoopHooks -- lifecycle callbacks injected into the loop
// ---------------------------------------------------------------------------
class LoopHooks {
 public:
  virtual ~LoopHooks() = default;

  virtual auto BeforeStep(int step) -> absl::StatusOr<std::optional<HookAction>> { return std::nullopt; }

  virtual auto AfterStep(int step) -> absl::Status { return absl::OkStatus(); }

  virtual auto ShouldContinueAfterStop(TurnStopReason reason) -> absl::StatusOr<bool> { return false; }

  virtual auto PrepareToolExecution(const ToolUseBlock& tool_call) -> absl::StatusOr<std::optional<HookAction>> {
    return std::nullopt;
  }

  virtual auto FinalizeToolResult(const ToolUseBlock& tool_call, const ToolResultBlock& result) -> absl::Status {
    return absl::OkStatus();
  }
};

// ---------------------------------------------------------------------------
// LoopEvent types -- emitted by the loop during execution
// ---------------------------------------------------------------------------
struct LoopStepStarted {
  int step = 0;
};

struct LoopStepEnded {
  int step = 0;
  TurnStopReason stop_reason = TurnStopReason::kEndTurn;
};

struct LoopAssistantDelta {
  std::string text;
};

struct LoopThinkingDelta {
  std::string text;
};

struct LoopToolCallStarted {
  std::string id;
  std::string name;
};

struct LoopToolCallDelta {
  std::string id;
  std::string input_json_delta;
};

struct LoopToolCallFinished {
  std::string id;
};

struct LoopToolResult {
  std::string tool_use_id;
  std::string content;
  bool is_error = false;
};

using LoopEvent = std::variant<LoopStepStarted, LoopStepEnded, LoopAssistantDelta, LoopThinkingDelta,
                               LoopToolCallStarted, LoopToolCallDelta, LoopToolCallFinished, LoopToolResult>;

// ---------------------------------------------------------------------------
// LoopEventDispatcher -- recorded (persisted) vs live (UI only) events
// ---------------------------------------------------------------------------
class LoopEventDispatcher {
 public:
  virtual ~LoopEventDispatcher() = default;

  virtual void Recorded(const LoopEvent& event) = 0;
  virtual void Live(const LoopEvent& event) = 0;
};

// ---------------------------------------------------------------------------
// NullEventDispatcher -- no-op implementation for testing
// ---------------------------------------------------------------------------
class NullEventDispatcher final : public LoopEventDispatcher {
 public:
  void Recorded(const LoopEvent&) override {}
  void Live(const LoopEvent&) override {}
};

// ---------------------------------------------------------------------------
// CallbackEventDispatcher -- forwards events to callbacks
// ---------------------------------------------------------------------------
class CallbackEventDispatcher final : public LoopEventDispatcher {
 public:
  using Callback = std::function<void(const LoopEvent&)>;

  explicit CallbackEventDispatcher(Callback recorded_cb, Callback live_cb = {})
      : recorded_cb_(std::move(recorded_cb)), live_cb_(std::move(live_cb)) {}

  void Recorded(const LoopEvent& event) override {
    if (recorded_cb_) recorded_cb_(event);
  }

  void Live(const LoopEvent& event) override {
    if (live_cb_) live_cb_(event);
  }

 private:
  Callback recorded_cb_;
  Callback live_cb_;
};

// ---------------------------------------------------------------------------
// TurnInput -- all dependencies passed to RunTurn()
// ---------------------------------------------------------------------------
struct TurnInput {
  std::string turn_id;
  CancellationToken cancellation;
  std::vector<Message> messages;
  LoopEventDispatcher* event_dispatcher = nullptr;
  std::vector<const Tool*> tools;
  LoopHooks* hooks = nullptr;
  int max_steps = 1000;
};

}  // namespace engine
}  // namespace codeharness
