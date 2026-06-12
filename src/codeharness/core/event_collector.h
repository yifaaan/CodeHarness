#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "codeharness/core/message.h"
#include "codeharness/core/overloaded.h"
#include "codeharness/provider/provider.h"

namespace codeharness {

// Accumulates a ProviderEvent stream into a single Message.
//
// Rules:
//   - AssistantTextDelta    -> append a TextBlock
//   - ThinkingDelta         -> append a ThinkingBlock
//   - ToolUseStarted        -> append an empty ToolUseBlock
//   - ToolUseInputDelta     -> append delta to registered ToolUseBlock input_json
//   - ToolUseFinished       -> validate id was registered
//   - MessageFinished       -> ignored (drives stream termination)
//
// Error handling: keeps the first error, subsequent errors don't overwrite.
// Callers get the result via Finalize().
class ProviderEventCollector {
 public:
  void OnEvent(const ProviderEvent& event) {
    std::visit(
        Overloaded{[this](const AssistantTextDelta& delta) { message_.content.emplace_back(TextBlock{delta.text}); },
                   [this](const ThinkingDelta& delta) { message_.content.emplace_back(ThinkingBlock{delta.text}); },
                   [this](const ToolUseStarted& started) {
                     if (tool_block_by_id_.contains(started.id)) {
                       SetError(absl::InvalidArgumentError("duplicate tool use id: " + started.id));
                       return;
                     }
                     tool_block_by_id_[started.id] = message_.content.size();
                     message_.content.emplace_back(
                         ToolUseBlock{.id = started.id, .name = started.name, .input_json = ""});
                   },
                   [this](const ToolUseInputDelta& delta) {
                     auto found = tool_block_by_id_.find(delta.id);
                     if (found == tool_block_by_id_.end()) {
                       SetError(absl::InvalidArgumentError("tool input delta before tool start: " + delta.id));
                       return;
                     }
                     auto* tool_use = std::get_if<ToolUseBlock>(&message_.content[found->second]);
                     if (tool_use == nullptr) {
                       SetError(absl::InternalError("tool block type mismatch"));
                       return;
                     }
                     tool_use->input_json += delta.input_json_delta;
                   },
                   [this](const ToolUseFinished& finished) {
                     if (!tool_block_by_id_.contains(finished.id)) {
                       SetError(absl::InvalidArgumentError("tool finished before tool start: " + finished.id));
                     }
                   },
                   [](const MessageFinished&) {}, [](const ProviderUsage&) {}},
        event);
  }

  bool HasError() const noexcept { return event_error_.has_value(); }

  absl::StatusOr<Message> Finalize() const {
    if (event_error_) {
      return *event_error_;
    }
    return message_;
  }

  Message& Message() noexcept { return message_; }

 private:
  void SetError(absl::Status status) {
    if (!event_error_) {
      event_error_ = std::move(status);
    }
  }

  codeharness::Message message_;
  std::unordered_map<std::string, std::size_t> tool_block_by_id_;
  std::optional<absl::Status> event_error_;
};

}  // namespace codeharness
