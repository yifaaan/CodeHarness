#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <variant>

#include "absl/status/status.h"
#include "codeharness/core/message.h"

namespace codeharness {

struct AssistantTextDelta {
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

struct MessageFinished {};

struct ProviderUsage {
  int input_tokens = 0;
  int output_tokens = 0;
  int total_tokens = 0;

  [[nodiscard]] auto normalized_total() const noexcept -> int {
    return total_tokens > 0 ? total_tokens : input_tokens + output_tokens;
  }
};

auto to_json(nlohmann::json& output, const ProviderUsage& usage) -> void;
auto from_json(const nlohmann::json& input, ProviderUsage& usage) -> void;

using ProviderEvent = std::variant<AssistantTextDelta, ToolUseStarted, ToolUseInputDelta, ToolUseFinished,
                                   MessageFinished, ProviderUsage>;

using ProviderEventSink = std::function<void(const ProviderEvent&)>;

class Provider {
 public:
  virtual ~Provider() = default;

  virtual auto stream(std::span<const Message> messages, const ProviderEventSink& sink) -> absl::Status = 0;
};

}  // namespace codeharness
