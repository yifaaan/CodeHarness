#pragma once

#include "codeharness/mailbox/mailbox.h"
#include "codeharness/tasks/task_manager.h"

#include <string>

// task_notification.h — worker 完成任务后发给 coordinator 的结果信封
//
// 上游 prompt 使用 <task-notification> XML 包装 worker 结果。这里保留这个
// 轻量格式，方便 coordinator 把 task_result mailbox 消息直接注入对话上下文。

namespace codeharness::coordinator
{

struct TaskNotification
{
    std::string task_id;
    std::string status;
    std::string summary;
    std::string result;
};

auto make_task_notification(const tasks::TaskRecord& record,
                            std::string result,
                            std::string summary = {}) -> TaskNotification;

auto task_notification_to_xml(const TaskNotification& notification) -> std::string;

auto make_task_result_message(std::string sender_id,
                              std::string recipient_id,
                              const TaskNotification& notification) -> mailbox::MailboxMessage;

} // namespace codeharness::coordinator
