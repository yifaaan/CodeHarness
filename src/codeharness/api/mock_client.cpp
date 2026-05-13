#include "codeharness/api/mock_client.h"

#include <absl/status/status.h>
#include <utility>

namespace codeharness::api {

    MockClient::MockClient(std::deque<Response> responses) : responses_{std::move(responses)} {}

    auto MockClient::stream_message(const MessageRequest& request, ApiStreamSink sink)
        -> absl::Status {
        requests_.push_back(request);

        if (responses_.empty()) {
            return absl::FailedPreconditionError("mock client has no queued response");
        }

        auto response = std::move(responses_.front());
        responses_.pop_front();

        for (const auto& block : response.message.content) {
            auto* text = std::get_if<engine::TextBlock>(&block);
            if (text && !text->text.empty()) {
                sink(engine::AssistantTextDelta{.text = text->text});
            }
        }

        sink(MessageComplete{
            .message = std::move(response.message),
            .usage = response.usage,
            .stop_reason = std::move(response.stop_reason),
        });
        return absl::OkStatus();
    }

    auto MockClient::requests() -> absl::Span<const MessageRequest> const { return requests_; }

}  // namespace codeharness::api
