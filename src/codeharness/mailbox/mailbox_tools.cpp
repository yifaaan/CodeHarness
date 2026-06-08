// mailbox_tools.cpp —— send_message 工具的实现
//
// 本文件实现了 SendMessageTool 类和 register_mailbox_tools() 注册函数。
// 设计模式完全遵循 tasks/task_tools.cpp 中 TaskCreateTool 的实现方式。

#include "codeharness/mailbox/mailbox_tools.h"

#include <nlohmann/json.hpp>
#include <nonstd/expected.hpp>

#include <memory>
#include <string>
#include <utility>

#include "codeharness/core/json_parse.h"

namespace codeharness::mailbox
{

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// 匿名命名空间：输入解析和响应构造的辅助函数
//
// 这些函数只在内部使用，不需要暴露给其他编译单元。
// 模式与 task_tools.cpp 中的匿名命名空间完全一致。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
namespace
{

// 解析 send_message 工具的输入参数。
//
// 输入 JSON 示例：
//   {"recipient_id": "task-123", "content": "hello", "sender_id": "task-456", "type": "user_message"}
//
// 使用 read_json_field 系列函数（来自 core/json_parse.h）来解析。
// 这些函数提供了统一的错误信息格式和类型检查。
auto parse_send_input(const nlohmann::json& input) -> Result<std::pair<std::string, MailboxMessage>>
{
    // recipient_id 是必填字段
    auto recipient_id = read_json_field<std::string>(input, "recipient_id", "send_message");
    if (!recipient_id)
    {
        return nonstd::make_unexpected(recipient_id.error());
    }

    // content 是必填字段
    auto content = read_json_field<std::string>(input, "content", "send_message");
    if (!content)
    {
        return nonstd::make_unexpected(content.error());
    }

    // sender_id 是可选字段——调用方可能不知道自己的 task ID
    auto sender_id = read_nullable_optional_json_field<std::string>(input, "sender_id", "send_message");
    if (!sender_id)
    {
        return nonstd::make_unexpected(sender_id.error());
    }

    // type 是可选字段，默认为 "user_message"
    auto type_str = read_json_field<std::string, JsonFieldMode::optional_with_default>(
        input, "type", "send_message", std::string{"user_message"});
    if (!type_str)
    {
        return nonstd::make_unexpected(type_str.error());
    }

    // 将字符串类型转为枚举——如果类型名无效，返回错误
    auto type = parse_message_type(*type_str);
    if (!type)
    {
        return nonstd::make_unexpected(type.error());
    }

    // 构造消息对象
    MailboxMessage msg;
    // sender_id 是 Result<optional<string>>，需要先解引用 Result（*sender_id 得到 optional<string>），
    // 再对 optional 调用 value_or（如果 optional 有值则取值，否则用 ""）。
    msg.sender_id = (*sender_id).value_or("");
    msg.content = std::move(*content);
    msg.type = *type;

    return std::make_pair(std::move(*recipient_id), std::move(msg));
}

} // namespace

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// SendMessageTool 实现
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

SendMessageTool::SendMessageTool(Mailbox& mailbox, const tasks::TaskManager* task_manager)
    : mailbox_{mailbox}
    , task_manager_{task_manager}
{
}

auto SendMessageTool::name() const -> std::string
{
    return "send_message";
}

auto SendMessageTool::description() const -> std::string
{
    return "Send a message to another agent's mailbox.";
}

auto SendMessageTool::is_read_only() const noexcept -> bool
{
    // send_message 向磁盘写入消息文件，属于写操作。
    // 在权限系统的 Default 模式下，写入操作需要用户确认；
    // 在 Plan 模式下，写入操作会被拒绝；
    // 在 FullAuto 模式下，写入操作自动允许。
    return false;
}

auto SendMessageTool::permission_target(const ToolRequest& request) const -> PermissionTarget
{
    // 从已解析的输入中提取 recipient_id，构造权限目标。
    //
    // 为什么是 command 而不是 path？
    //   send_message 不操作文件路径，而是操作「向谁发送」这个语义。
    //   将 recipient_id 放在 command 字段中，权限系统可以基于收件人做细粒度控制。
    //
    //   例如：权限规则可以写成 "allow send_message:task-worker-*" 来只允许
    //   向 worker 角色的 agent 发送消息。
    PermissionTarget target;

    // 尝试从已解析的输入中获取 recipient_id
    if (request.parsed_input.contains("recipient_id") && request.parsed_input.at("recipient_id").is_string())
    {
        target.command = "send_message:" + request.parsed_input.at("recipient_id").get<std::string>();
    }
    else
    {
        // 如果输入未解析或缺少 recipient_id，使用通用标识
        target.command = "send_message";
    }

    return target;
}

auto SendMessageTool::execute(const ToolRequest& request, const ToolContext& context) const
    -> Result<ToolResponse>
{
    // 第一步：解析输入
    auto input = parse_send_input(request.parsed_input);
    if (!input)
    {
        return nonstd::make_unexpected(input.error());
    }

    auto& [recipient_id, msg] = *input;

    // sender_id 自动注入：LLM 显式传入的值优先，否则使用 ToolContext 中
    // Engine 注入的 agent 身份。
    if (msg.sender_id.empty() && !context.sender_id.empty())
    {
        msg.sender_id = context.sender_id;
    }

    // 第二步：如果提供了 TaskManager，验证收件人是否存在
    //
    // 为什么这是可选的？
    //   - 在单元测试中，可能没有 TaskManager。
    //   - 在某些部署场景中，消息的收件人可能不在本地 TaskManager 的管辖范围内
    //     （例如远程 Agent）。
    //   - 即使没有验证，Mailbox::send() 也能正常工作——它只是创建一个文件。
    if (task_manager_ != nullptr)
    {
        auto task = task_manager_->get_task(recipient_id);
        if (!task)
        {
            // get_task 本身失败（IO 错误等）
            return nonstd::make_unexpected(task.error());
        }
        if (!task->has_value())
        {
            // 收件人不存在——返回有意义的错误信息给 LLM
            return ToolResponse{
                .tool_use_id = request.id,
                .content = "Recipient task not found: " + recipient_id,
                .is_error = true,
            };
        }
    }

    // 第三步：通过 Mailbox 投递消息
    auto sent = mailbox_.send(recipient_id, std::move(msg));
    if (!sent)
    {
        return nonstd::make_unexpected(sent.error());
    }

    // 第四步：返回成功响应，包含完整的消息信息
    const nlohmann::json sent_json = *sent;
    return ToolResponse{
        .tool_use_id = request.id,
        .content = sent_json.dump(2),
    };
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// register_mailbox_tools —— 注册所有 mailbox 工具到 ToolRegistry
//
// 调用示例（在 CLI 层）：
//   Mailbox mailbox{default_mailbox_root().value()};
//   ToolRegistry registry;
//   register_mailbox_tools(registry, mailbox, &task_manager);
//
// 之后，当 LLM 在 agent loop 中发出 {"name": "send_message", ...} 时，
// Engine 会通过 registry.find("send_message") 找到本工具并执行。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
auto register_mailbox_tools(ToolRegistry& registry, Mailbox& mailbox, const tasks::TaskManager* task_manager)
    -> void
{
    registry.add(std::make_unique<SendMessageTool>(mailbox, task_manager));
}

} // namespace codeharness::mailbox
