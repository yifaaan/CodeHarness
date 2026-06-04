#pragma once

#include "codeharness/mailbox/mailbox.h"

#include <string_view>
#include <vector>

// message_consumer.h — worker 每轮开始时消费 mailbox 消息
//
// 当前 Mailbox 只负责文件系统 inbox 的读写；worker message consumer 负责把
// “读取未读消息 + 标记已读 + 按类型分组”这一步固化下来，供后续 agent loop 在每轮开始时调用。

namespace codeharness::mailbox
{

struct WorkerMailboxDrain
{
    std::vector<MailboxMessage> user_messages;
    std::vector<MailboxMessage> task_results;
    std::vector<MailboxMessage> permission_requests;
    std::vector<MailboxMessage> permission_responses;
    std::vector<MailboxMessage> shutdown_messages;
    std::vector<MailboxMessage> idle_notifications;

    [[nodiscard]] auto shutdown_requested() const noexcept -> bool;
};

// 读取 worker_id inbox 中的未读消息，逐条标记为已读，并按 MessageType 分类返回。
// 如果 inbox 不存在，返回空 WorkerMailboxDrain。
auto drain_worker_mailbox(Mailbox& mailbox, std::string_view worker_id) -> Result<WorkerMailboxDrain>;

} // namespace codeharness::mailbox
