#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <variant>

#include "absl/status/status.h"
#include "codeharness/core/message.h"

namespace codeharness {

// ---------------------------------------------------------------------------
// FinishReason — unified provider stop reason
// ---------------------------------------------------------------------------
enum class FinishReason {
  kCompleted,
  kToolCalls,
  kTruncated,
  kFiltered,
  kPaused,
  kUnknown,
};

FinishReason FinishReasonFromString(std::string_view value);
std::string_view FinishReasonToString(FinishReason reason);

// ---------------------------------------------------------------------------
// ModelCapability — describes what a model can do
// ---------------------------------------------------------------------------
struct ModelCapability {
  bool image_in = false;
  bool video_in = false;
  bool audio_in = false;
  bool thinking = false;
  bool tool_use = true;
  int max_context_tokens = 0;
};

// ---------------------------------------------------------------------------
// Streaming event types (same wire format as before, extended)
// ---------------------------------------------------------------------------

struct AssistantTextDelta {
  std::string text;
};

struct ThinkingDelta {
  std::string text;
};

struct ToolUseStarted {
  std::string id;
  std::string name;
};

struct ToolUseInputDelta {
  std::string id;
  std::string input_json_delta;
};

struct ToolUseFinished {
  std::string id;
};

struct MessageFinished {
  FinishReason reason = FinishReason::kCompleted;
};

struct ProviderUsage {
  int input_tokens = 0;
  int output_tokens = 0;
  int total_tokens = 0;

  [[nodiscard]] auto NormalizedTotal() const noexcept -> int {
    return total_tokens > 0 ? total_tokens : input_tokens + output_tokens;
  }
};

void to_json(nlohmann::json& output, const ProviderUsage& usage);
void from_json(const nlohmann::json& input, ProviderUsage& usage);

using ProviderEvent = std::variant<AssistantTextDelta, ThinkingDelta, ToolUseStarted, ToolUseInputDelta,
                                   ToolUseFinished, MessageFinished, ProviderUsage>;

using ProviderEventSink = std::function<void(const ProviderEvent&)>;

// ---------------------------------------------------------------------------
// ChatProvider — unified LLM provider interface
// ---------------------------------------------------------------------------
class ChatProvider {
 public:
  virtual ~ChatProvider() = default;

  // Provider identity.
  virtual std::string_view Name() const = 0;
  virtual std::string_view ModelName() const = 0;

  // Core streaming call — the provider emits events via the sink.
  virtual absl::Status Stream(std::span<const Message> messages, const ProviderEventSink& sink) = 0;

  // Capability query.
  virtual ModelCapability Capability() const = 0;
};

}  // namespace codeharness
