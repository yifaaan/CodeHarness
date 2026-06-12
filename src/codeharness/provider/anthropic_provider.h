#pragma once

#include <span>
#include <string>
#include <vector>

#include "codeharness/network/http_client.h"
#include "codeharness/provider/provider.h"
#include "codeharness/provider/provider_config.h"

namespace codeharness {

class AnthropicProvider final : public ChatProvider {
 public:
  AnthropicProvider(ProviderConfig config, std::vector<std::pair<std::string, std::string>> tool_descriptions = {});

  std::string_view Name() const override { return "anthropic"; }
  std::string_view ModelName() const override;

  absl::Status Stream(std::span<const Message> messages, const ProviderEventSink& sink) override;

  ModelCapability Capability() const override {
    return ModelCapability{
        .thinking = true,
        .tool_use = true,
        .max_context_tokens = 200000,
    };
  }

 private:
  ProviderConfig config_;
  std::vector<std::pair<std::string, std::string>> tool_descriptions_;
  network::HttpClient http_;
};

}  // namespace codeharness
