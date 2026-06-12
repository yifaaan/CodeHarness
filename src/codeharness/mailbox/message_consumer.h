#pragma once

#include "codeharness/mailbox/mailbox.h"

#include <string_view>
#include <vector>

//==============================================================================
// message_consumer.h — Mailbox 消息消费器
//
// 架构角色：消息处理层
// 职责：封装”读取未读消息 → 标记已读 → 按类型分组”这一固定模式。
//
// 设计原理：
//   Mailbox 本身是纯文件系统操作（写消息文件 + 扫描 inbox）。
//   WorkerMailboxDrain 加上 drain_worker_mailbox 函数将批量读取/标记/
//   分类封装为一步操作，避免每个 worker 重复这一模式。
//
// WorkerMailboxDrain 的消息分类：
//   user_messages          — 用户发给此 agent 的消息
//   task_results           — 其他 agent 完成的任务结果
//   permission_requests    — 权限请求（其他 agent 请求此 agent 授权）
//   permission_responses   — 权限响应（其他 agent 的授权决定）
//   shutdown_messages      — 关闭命令（收到后 agent 应终止）
//   idle_notifications     — 空闲通知（其他 agent 暂时无工作）
//
//   shutdown_requested() — 便捷方法，检查是否有 shutdown 消息。
//     主 agent 循环通常在每次迭代开始时调用 drain_worker_mailbox，
//     然后检查 shutdown_requested() 来决定是否退出。
//
//   为什么 mailbox 在文件系统上？
//     进程间通信（IPC）的一种轻量选择。相比 Unix socket / 共享内存，
//     文件系统 mailbox 的优点：
//     - 跨平台（Windows/Linux/macOS 都可用）
//     - 进程崩溃不丢失消息（消息已持久化）
//     - 调试时可以直接看文件内容
//     缺点是延迟较高（毫秒级 vs 微秒级），但对 agent 场景足够。

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
auto drain_worker_mailbox(Mailbox& mailbox, std::string_view worker_id) -> absl::StatusOr<WorkerMailboxDrain>;

} // namespace codeharness::mailbox
