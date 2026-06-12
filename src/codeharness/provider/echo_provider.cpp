#include "codeharness/provider/echo_provider.h"

#include <optional>

#include "absl/status/status.h"

namespace codeharness {

absl::Status EchoProvider::Stream(std::span<const Message> messages, const ProviderEventSink& sink) {
  std::optional<std::string> latest_user_text;

  for (int index = messages.size() - 1; index >= 0; index--) {
    const auto& message = messages[index];
    if (message.role != Role::kUser) continue;

    auto text = CollectText(message);
    if (!text.empty()) {
      latest_user_text = std::move(text);
      break;
    }
  }

  if (!latest_user_text) {
    return absl::InvalidArgumentError("prompt is empty");
  }

  sink(AssistantTextDelta{*latest_user_text});
  sink(MessageFinished{});
  return absl::OkStatus();
}

}  // namespace codeharness
