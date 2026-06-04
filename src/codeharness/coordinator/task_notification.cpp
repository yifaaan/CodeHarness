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
