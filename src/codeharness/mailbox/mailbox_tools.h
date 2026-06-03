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

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
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
//   显式传入。如果调用方不知道自己的 task ID（例如顶层 CLI 调用），
//   则留空，Mailbox 会将 sender_id 设为空字符串。
//   未来当 Engine 支持「当前 Agent 上下文」时，sender_id 可以自动注入。
//
// 为什么 TaskManager* 是可选的？
//   在测试环境中，可能没有 TaskManager 实例可用。
//   工具不应因为缺少 TaskManager 而无法工作——只是在有 TaskManager 时
//   会额外验证收件人是否存在，给出更友好的错误信息。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
class SendMessageTool final : public Tool
{
public:
    // 构造函数。
    // mailbox: 消息队列实例（必须比本工具活得更久——引用语义）
    // task_manager: 可选的任务管理器，用于验证收件人是否存在
    explicit SendMessageTool(Mailbox& mailbox, const tasks::TaskManager* task_manager);

    auto name() const -> std::string override;
    auto description() const -> std::string override;

    // send_message 是写操作（它向磁盘写入消息文件），所以不是只读工具。
    // 这影响权限系统的行为：在 Default 模式下，写入操作需要用户确认。
    auto is_read_only() const noexcept -> bool override;

    // 权限目标：command = "send_message:{recipient_id}"
    //
    // 为什么用 command 而不是 path？
    //   发送消息不涉及文件路径，而是涉及「谁在向谁发送」。
    //   用 command 字段表达这个语义，权限系统可以基于 recipient_id
    //   做更细粒度的控制（例如：只允许向特定 Agent 发送消息）。
    auto permission_target(const ToolRequest& request) const -> PermissionTarget override;

    // 执行发送消息的操作：
    //   1. 解析输入 JSON（recipient_id, content, sender_id, type）
    //   2. 验证 recipient_id 非空
    //   3. 如果有 TaskManager，验证收件人存在
    //   4. 调用 Mailbox::send() 投递消息
    //   5. 返回包含完整消息信息的 ToolResponse
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;

private:
    Mailbox& mailbox_;
    const tasks::TaskManager* task_manager_;
};

// 注册所有 mailbox 相关工具到 ToolRegistry。
//
// 这是一个便捷函数，遵循 register_task_tools() 的模式。
// 调用方（通常是 CLI 层）只需一行代码即可注册所有 mailbox 工具。
//
// 为什么需要注册函数而不是让调用方手动 registry.add()？
//   - 封装：调用方不需要知道有哪些具体工具类。
//   - 一致性：所有模块的工具注册方式相同。
//   - 扩展性：将来新增 mailbox 工具时，只需修改这个函数，调用方无需改动。
auto register_mailbox_tools(ToolRegistry& registry, Mailbox& mailbox, const tasks::TaskManager* task_manager) -> void;

} // namespace codeharness::mailbox
