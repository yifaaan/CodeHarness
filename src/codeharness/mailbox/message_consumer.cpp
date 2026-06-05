//==============================================================================
// message_consumer.cpp — Mailbox 消息消费实现
//
// drain_worker_mailbox 的完整操作序列：
//   1. mailbox.poll(worker_id, mark_read=false)：扫描 inbox 获取所有未读消息
//   2. 对每条消息：mailbox.mark_read(id, message_id) → 标记已读
//   3. 按 MessageType 分类放入 WorkerMailboxDrain 的对应字段
//
// 为什么分两步（poll + mark_read）而不是一步"读取并标记"？
//   因为 mailbox 层的设计原则是"底层接口语义最小化"——poll 只做读，
//   mark_read 只做标记，组合成 drain 是 consumer 层的责任。
//   这样 mailbox 层可以被不同的消费策略复用（例如"先预览再确认"）。
//
// 注意：如果在 mark_read 和分类之间进程崩溃，已读的消息在重启后
// 不会被二次消费（poll 返回的是未读消息）。这是"至少一次"语义——
// 消息最多丢，不会重复处理。
//==============================================================================

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
