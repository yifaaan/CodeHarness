#pragma once

#include "codeharness/core/error.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// mailbox.h — 文件系统消息队列（Mailbox）
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Agent 间通信的核心
//
// 在多 Agent 架构中，每个 Agent 可能运行在独立的子进程中（TaskManager
// 的 local_agent 模式），需要一种通信机制。
//
// 选择「文件系统队列」而非「内存队列」，原因有三：
//
//   1. 跨进程：两个独立进程可以读写同一个文件系统目录，无需额外基础设施。
//   2. 崩溃安全：如果 Coordinator 进程崩溃，消息仍保存在磁盘上；重启后可以
//      继续读取未处理的消息。
//   3. 可调试性：开发者可以直接浏览 ~/.codeharness/data/mailboxes/ 目录，
//      查看每条消息的内容和时间顺序，方便排查问题。
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// 【磁盘上的目录结构】
//
//   root/                        <-- Mailbox 构造时指定的根目录
//     {task_id}/                 <-- 每个 Agent 任务一个子目录
//       inbox/                   <-- 收件箱（当前只读 inbox，未来可扩展 outbox/）
//         000001.json            <-- 消息文件，按序号命名
//         000002.json
//         000003.json
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// 【消息的 JSON 格式】
//
//   {
//     "id": "msg-a1b2c3d4",          // 唯一消息 ID，8 位十六进制
//     "type": "user_message",        // 消息类型（见 MessageType 枚举）
//     "sender_id": "task-abc123",    // 发送者的 task ID
//     "recipient_id": "task-def456", // 接收者的 task ID
//     "content": "请分析认证模块",    // 消息正文（纯文本）
//     "timestamp": "2026-06-03T14:30:00Z",  // ISO 8601 UTC 时间戳
//     "read": false                  // 是否已被读取
//   }
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
namespace codeharness::mailbox
{

enum class MessageType
{
    UserMessage,          // 普通文本消息：一个 Agent 向另一个 Agent 发送指令或信息
    TaskResult,           // 任务结果：Worker 完成任务后向 Coordinator 报告结果
    PermissionRequest,    // 权限请求：Worker 请求 Coordinator 代为向用户请求权限
    PermissionResponse,   // 权限响应：Coordinator 回复权限请求（允许/拒绝）
    Shutdown,             // 关闭指令：Coordinator 通知 Worker 停止工作
    IdleNotification,     // 空闲通知：Worker 报告自己已完成所有工作
};

// 枚举 ↔ 字符串转换函数（在 mailbox.cpp 中实现）
// 用于 JSON 序列化/反序列化的边界层。
auto message_type_name(MessageType type) -> std::string_view;
auto parse_message_type(std::string_view value) -> absl::StatusOr<MessageType>;

// MailboxMessage —— 一条消息的完整数据
struct MailboxMessage
{
    std::string id;                            // 唯一消息 ID，格式 "msg-" + 8 位十六进制
    MessageType type = MessageType::UserMessage; // 消息类型
    std::string sender_id;                     // 发送者的 task ID
    std::string recipient_id;                  // 接收者的 task ID
    std::string content;                       // 消息正文
    std::string timestamp;                     // ISO 8601 UTC 时间戳
    bool read = false;                         // 是否已被 poll 标记为已读
};

auto to_json(nlohmann::json& output, const MailboxMessage& msg) -> void;
auto from_json(const nlohmann::json& input, MailboxMessage& msg) -> void;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Mailbox —— 文件系统消息队列
//
// 线程安全：
//   所有 public 方法内部通过 mutex 保护。对于「同一进程内的多线程并发 send」
//   是安全的。跨进程的并发由文件系统的 rename 原子性保证（见 send() 的注释）。
//
// 为什么用 poll() 而不是阻塞的 receive()？
//   - 阻塞 receive 需要 condition variable 或文件系统 watcher（Linux inotify、
//     Windows ReadDirectoryChangesW），复杂度较高。
//   - 当前设计是：Engine 的每一轮 agent loop 开始时调用 poll()，检查新消息。
class Mailbox
{
public:
    // 构造 Mailbox，指定根目录。
    explicit Mailbox(std::filesystem::path root);

    ~Mailbox();

    Mailbox(const Mailbox&) = delete;
    auto operator=(const Mailbox&) -> Mailbox& = delete;

    Mailbox(Mailbox&&) noexcept;
    auto operator=(Mailbox&&) noexcept -> Mailbox&;

    // 返回根目录路径
    [[nodiscard]] auto root() const -> const std::filesystem::path&;

    // 向 recipient_id 的收件箱投递一条消息。
    //
    // 写入过程（原子写，与 tools/text_file.cpp 中的 AtomicWriteTextFile 相同）：
    //   1. 确保 inbox 目录存在
    //   2. 扫描 inbox，找到最大的序号
    //   3. 生成消息 ID 和时间戳，填入 message
    //   4. 将 JSON 写入 .tmp 文件
    //   5. flush 并关闭文件
    //   6. rename .tmp → 最终文件名（操作系统保证 rename 的原子性）
    //
    // 返回值：填充了 id、recipient_id、timestamp 的消息副本。
    auto send(const std::string& recipient_id, MailboxMessage message) -> absl::StatusOr<MailboxMessage>;


    // 读取 task_id 收件箱中的消息。
    // - unread_only = true 时，只返回 read 字段为 false 的消息。
    // - 返回的消息按文件名排序（即按发送时间排序）。
    // - 如果 inbox 目录不存在，返回空 vector（不是错误）。
    auto poll(const std::string& task_id, bool unread_only = true) const -> absl::StatusOr<std::vector<MailboxMessage>>;

    // 将指定消息标记为已读。
    // 原地更新 JSON 文件（同样是原子写：写 .tmp → rename）。
    auto mark_read(const std::string& task_id, const std::string& message_id) -> absl::Status;

    // 清空 task_id 的收件箱：删除所有 .json 消息文件。
    // 忽略单个文件删除失败（例如文件已被其他进程删除）。
    auto clear(const std::string& task_id) -> absl::Status;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 返回默认的 Mailbox 根目录路径。
// 即 config::data_dir() / "mailboxes"。
auto default_mailbox_root() -> std::filesystem::path;

} // namespace codeharness::mailbox
