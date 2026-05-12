#pragma once

#include <chrono>
#include <string>

#include "codeharness/api/client.h"

namespace codeharness::api {

    struct OpenAIClientOptions {
        std::string api_key;
        std::string base_url{"https://api.openai.com/v1"};
        std::chrono::seconds timeout{60};
    };

    class OpenAIClient final : public Client {
    public:
        explicit OpenAIClient(OpenAIClientOptions options);

        auto stream_message(const MessageRequest& request, ApiStreamSink sink) -> void override;

    private:
        OpenAIClientOptions options_;
    };

}  // namespace codeharness::api