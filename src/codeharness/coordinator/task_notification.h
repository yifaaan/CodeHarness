#pragma once

#include "codeharness/mailbox/mailbox.h"
#include "codeharness/tasks/task_manager.h"

#include <string>

//==============================================================================
// task_notification.h — agent 任务结果的通知格式
//
// 架构角色：消息格式层
// 职责：定义子 agent 完成任务后，通过 mailbox 发送给 coordinator 的
//       结果信封格式。使用 XML 序列化以便于 LLM 解析。
//
// 设计原理：
//   当子 agent 完成任务后，它把结果写成 <task-notification> XML 消息，
//   发送到 coordinator 的 mailbox。coordinator 在下一轮 drain mailbox 时
//   收到这个 XML，可以直接注入到主 agent 的对话上下文中。
//
//   XML 格式（而非 JSON）的选择原因：
//   上游 prompt 模板使用 XML 标签体系嵌入上下文（如 <task-notification>、
//   <permission-result> 等）。LLM 对这些 XML 标签的识别更可靠。
//   使用相同的 XML 格式意味着 coordinator 可以直接把 mailbox 消息内容
//   原样粘贴到对话中，无需格式转换。
//
// 数据结构：
//   TaskNotification:
//     task_id  — 任务标识
//     status   — 任务状态（completed/failed/killed）
//     summary  — 任务摘要（用于快速预览）
//     result   — 完整结果内容（通常是子 agent 的最终输出）

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
