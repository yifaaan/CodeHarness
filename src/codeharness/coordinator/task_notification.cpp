//==============================================================================
// task_notification.cpp — 任务通知的 XML 序列化
//
// make_task_notification：
//   从 TaskRecord + result + summary 构造 TaskNotification。
//   如果 summary 为空，自动降级使用 record.description 或默认文本。
//
// task_notification_to_xml：
//   使用 pugixml 库构造 XML 字符串：
//     <task-notification>
//       <task-id>a3f7a1b2</task-id>
//       <status>completed</status>
//       <summary>Fixed the off-by-one bug</summary>
//       <result>... actual output ...</result>
//     </task-notification>
//
// make_task_result_message：
//   将 TaskNotification 包装成 MailboxMessage，类型为 TaskResult。
//   这样 coordinator drain mailbox 时可以通过 MessageType::TaskResult
//   分类识别。
//
// 为什么使用 XML 而不是 JSON？
//   LLM 系统提示词中使用了 <task-notification> 标签体系。LLM 对 XML 格式
//   （标签 + 内容）的识别和解析比 JSON 更可靠。这个 XML 会被直接嵌入到
//   下一轮对话的上下文提示词中。
//==============================================================================

#include "codeharness/coordinator/task_notification.h"

#include "codeharness/core/strings.h"

#include <pugixml.hpp>

#include <sstream>
#include <string>
#include <utility>

namespace codeharness::coordinator
{
namespace
{

auto append_xml_element(pugi::xml_node parent, const char* name, const std::string& value) -> void
{
    parent.append_child(name).append_child(pugi::node_pcdata).set_value(value.c_str());
}

} // namespace

auto make_task_notification(const tasks::TaskRecord& record,
                            std::string result,
                            std::string summary) -> TaskNotification
{
    auto status = std::string{tasks::task_status_name(record.status)};
    auto notification_summary = std::string{trim(summary)};
    if (notification_summary.empty())
    {
        notification_summary = std::string{trim(record.description)};
    }
    if (notification_summary.empty())
    {
        notification_summary = "Task " + record.id + " " + status;
    }

    return TaskNotification{
        .task_id = record.id,
        .status = std::move(status),
        .summary = std::move(notification_summary),
        .result = std::move(result),
    };
}

auto task_notification_to_xml(const TaskNotification& notification) -> std::string
{
    pugi::xml_document document;
    auto root = document.append_child("task-notification");
    append_xml_element(root, "task-id", notification.task_id);
    append_xml_element(root, "status", notification.status);
    append_xml_element(root, "summary", notification.summary);
    append_xml_element(root, "result", notification.result);

    std::ostringstream output;
    document.save(output, "", pugi::format_raw);
    return output.str();
}

auto make_task_result_message(std::string sender_id,
                              std::string recipient_id,
                              const TaskNotification& notification) -> mailbox::MailboxMessage
{
    return mailbox::MailboxMessage{
        .type = mailbox::MessageType::TaskResult,
        .sender_id = std::move(sender_id),
        .recipient_id = std::move(recipient_id),
        .content = task_notification_to_xml(notification),
    };
}

} // namespace codeharness::coordinator
