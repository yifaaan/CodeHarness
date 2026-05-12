#pragma once

#include <deque>
#include <string>
#include <vector>

#include "absl/types/span.h"
#include "codeharness/api/client.h"
#include "codeharness/engine/stream_event.h"

namespace codeharness::api {
    class MockClient final : public Client {
    public:
        struct Response {
            engine::ConversationMessage message;
            engine::UsageSnapshot usage;
            std::string stop_reason{"end_turn"};
        };

        explicit MockClient(std::deque<Response> responses);

        auto stream_message(const MessageRequest& request, ApiStreamSink sink) -> void override;

        [[nodiscard]] auto requests() -> absl::Span<const MessageRequest> const;

    private:
        std::deque<Response> responses_;
        std::vector<MessageRequest> requests_;
    };

}  // namespace codeharness::api
