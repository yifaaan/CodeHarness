#include "codeharness/mailbox/message_consumer.h"

#include <nonstd/expected.hpp>

#include <string>
#include <utility>

namespace codeharness::mailbox
{

auto WorkerMailboxDrain::shutdown_requested() const noexcept -> bool
{
    return !shutdown_messages.empty();
}

auto drain_worker_mailbox(Mailbox& mailbox, std::string_view worker_id) -> Result<WorkerMailboxDrain>
{
    const auto id = std::string{worker_id};
    auto pending = mailbox.poll(id, true);
    if (!pending)
    {
        return nonstd::make_unexpected(pending.error());
    }

    WorkerMailboxDrain drain;
    for (auto& message : *pending)
    {
        auto marked = mailbox.mark_read(id, message.id);
        if (!marked)
        {
            return nonstd::make_unexpected(marked.error());
        }

        switch (message.type)
        {
        case MessageType::UserMessage:
            drain.user_messages.push_back(std::move(message));
            break;
        case MessageType::TaskResult:
            drain.task_results.push_back(std::move(message));
            break;
        case MessageType::PermissionRequest:
            drain.permission_requests.push_back(std::move(message));
            break;
        case MessageType::PermissionResponse:
            drain.permission_responses.push_back(std::move(message));
            break;
        case MessageType::Shutdown:
            drain.shutdown_messages.push_back(std::move(message));
            break;
        case MessageType::IdleNotification:
            drain.idle_notifications.push_back(std::move(message));
            break;
        }
    }

    return drain;
}

} // namespace codeharness::mailbox
