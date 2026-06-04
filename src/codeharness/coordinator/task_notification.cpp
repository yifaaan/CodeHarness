#include "codeharness/coordinator/task_notification.h"

#include "codeharness/core/strings.h"

#include <string_view>
#include <utility>

namespace codeharness::coordinator
{
namespace
{

auto append_xml_escaped(std::string& output, std::string_view text) -> void
{
    for (const auto ch : text)
    {
        switch (ch)
        {
        case '&':
            output += "&amp;";
            break;
        case '<':
            output += "&lt;";
            break;
        case '>':
            output += "&gt;";
            break;
        case '"':
            output += "&quot;";
            break;
        case '\'':
            output += "&apos;";
            break;
        default:
            output += ch;
            break;
        }
    }
}

auto append_xml_element(std::string& output, std::string_view name, std::string_view value) -> void
{
    output += '<';
    output += name;
    output += '>';
    append_xml_escaped(output, value);
    output += "</";
    output += name;
    output += ">\n";
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
    std::string output;
    output.reserve(notification.task_id.size() + notification.status.size() + notification.summary.size()
                   + notification.result.size() + 128);

    output += "<task-notification>\n";
    append_xml_element(output, "task-id", notification.task_id);
    append_xml_element(output, "status", notification.status);
    append_xml_element(output, "summary", notification.summary);
    append_xml_element(output, "result", notification.result);
    output += "</task-notification>";

    return output;
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
