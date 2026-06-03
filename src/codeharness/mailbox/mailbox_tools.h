#pragma once

#include "codeharness/mailbox/mailbox.h"
#include "codeharness/tasks/task_manager.h"
#include "codeharness/tools/tool.h"
#include "codeharness/tools/tool_registry.h"


// 在多 Agent 系统中，Agent 需要互相通信。例如：
//   - Coordinator（总指挥）向 Worker 发送任务指令
//   - Worker 完成后向 Coordinator 报告结果
//   - Worker 遇到权限问题时向 Coordinator 请求帮助
//
// send_message 是 LLM 可以调用的工具之一
namespace codeharness::mailbox
{

// SendMessageTool —— 让 Agent 能够向另一个 Agent 发送消息
//
// Engine 在 agent loop 中遇到 ToolUseBlock 时，通过 ToolRegistry 查找并调用它。
//
// 输入格式：
//   {
//     "recipient_id": "task-def456",        // 必填：收件人的 task ID
//     "content": "请分析认证模块",            // 必填：消息正文
//     "sender_id": "task-abc123",           // 可选：发送者的 task ID
//     "type": "user_message"                // 可选：消息类型，默认 "user_message"
//   }
//
// 为什么 sender_id 是可选的？
//   在当前设计中，sender_id 需要由调用方（通常是 Engine 或 Coordinator）
//   显式传入。
//   TODO:当 Engine 支持「当前 Agent 上下文」时，sender_id 可以自动注入。

class SendMessageTool final : public Tool
{
public:
    // mailbox: 消息队列
    // task_manager: 用于验证收件人是否存在
    explicit SendMessageTool(Mailbox& mailbox, const tasks::TaskManager* task_manager);

    auto name() const -> std::string override;
    auto description() const -> std::string override;

    // send_message 是写操作
    auto is_read_only() const noexcept -> bool override;

    // 权限目标：command = "send_message:{recipient_id}"
    auto permission_target(const ToolRequest& request) const -> PermissionTarget override;

    //   1. 解析输入 JSON（recipient_id, content, sender_id, type）
    //   2. 如果有 TaskManager，验证收件人存在
    //   3. 调用 Mailbox::send() 投递消息
    //   4. 返回包含完整消息信息的 ToolResponse
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;

private:
    Mailbox& mailbox_;
    const tasks::TaskManager* task_manager_;
};

// 注册所有 mailbox 相关工具到 ToolRegistry。
auto register_mailbox_tools(ToolRegistry& registry, Mailbox& mailbox, const tasks::TaskManager* task_manager) -> void;

} // namespace codeharness::mailbox
