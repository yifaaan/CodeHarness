#pragma once

#include <absl/status/status.h>

#include <memory>
#include <optional>
#include <string>

#include "chat_provider.h"
#include "http_client.h"
#include "types.h"

namespace codeharness::llm {

struct OpenAiConfig {
  std::string api_key;
  std::string host = "api.openai.com";
  std::string path = "/v1/chat/completions";
  std::string model;
  std::optional<ThinkingEffort> thinking;
  int max_completion_tokens = 0;
};

class OpenAiProvider : public ChatProvider {
 public:
  OpenAiProvider(OpenAiConfig config, HttpClient* http);

  std::string Name() const override;
  std::string ModelName() const override;
  std::optional<ThinkingEffort> ThinkingEffortLevel() const override;

  absl::Status Generate(std::string_view system_prompt, std::span<const Tool> tools, std::span<const Message> history,
                        const StreamCallbacks& callbacks, std::stop_token stop_token = {}) override;

 private:
  OpenAiConfig config_;
  HttpClient* http_;
};

}  // namespace codeharness::llm
