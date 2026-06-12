#pragma once

#include "codeharness/provider/provider.h"

namespace codeharness {

class EchoProvider final : public ChatProvider {
 public:
  std::string_view Name() const override { return "echo"; }
  std::string_view ModelName() const override { return "echo"; }

  absl::Status Stream(std::span<const Message> messages, const ProviderEventSink& sink) override;

  ModelCapability Capability() const override {
    return ModelCapability{
        .tool_use = true,
        .max_context_tokens = 4096,
    };
  }
};

}  // namespace codeharness
