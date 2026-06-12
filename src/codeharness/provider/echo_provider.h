#pragma once

#include "codeharness/provider/provider.h"

namespace codeharness {

class EchoProvider final : public Provider {
 public:
  auto stream(std::span<const Message> messages, const ProviderEventSink& sink) -> absl::Status override;
};

}  // namespace codeharness