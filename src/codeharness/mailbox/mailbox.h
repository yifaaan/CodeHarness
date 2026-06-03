// mailbox.h — 文件系统消息队列（Mailbox）
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// 【为什么需要 Mailbox？——Agent 间通信的核心基础设施】
//
// 在多 Agent 架构中，每个 Agent 可能运行在独立的子进程中（参见 TaskManager
// 的 local_agent 模式）。进程之间无法共享内存，因此需要一种通信机制。
//
// 我们选择「文件系统队列」而非「内存队列」，原因有三：
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
// 为什么用 000001、000002 这种递增序号命名，而不是 timestamp_uuid？
//   - 文件名的字典序（lexicographic order）天然等于时间序，排序不需要额外工作。
//   - 不依赖系统时钟，避免了多机部署时的时钟偏差问题。
//   - 人类可以直接看出消息的先后顺序。
//
// 为什么 inbox/ 是子目录而不是直接放在 task_id/ 下？
//   - 向前兼容：未来可能需要在同一目录下增加 outbox/、state/、locks/ 等子目录。
//   - 职责清晰：扫描 task_id/ 目录时，不会混淆消息文件和其他数据。
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
// 为什么 content 是字符串而不是嵌套对象？
//   - 与工具响应（ToolResponse）的设计保持一致——工具输出也是字符串。
//   - 发送方自行决定 content 的格式（纯文本、JSON 编码的结构化数据等），
//     Mailbox 层不强制约束，保持灵活性。
//
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#pragma once

#include "codeharness/core/result.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness::mailbox
{

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// MessageType —— 消息类型枚举
//
// 为什么用 enum class 而不是字符串常量？
//   - 编译期类型安全：拼写错误会在编译时报错，不会等到运行时才发现。
//   - switch 语句可以穷举所有分支，编译器会警告遗漏的情况。
//   - 这与 TaskType/TaskStatus 的设计模式一致。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
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
auto parse_message_type(std::string_view value) -> Result<MessageType>;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// MailboxMessage —— 一条消息的完整数据
//
// 这是一个简单的值类型（value type），所有字段都是 public 的。
// 通过 nlohmann/json 的 ADL to_json/from_json 自定义序列化（在 mailbox.cpp 中）。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
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

// nlohmann/json ADL 序列化钩子（在 mailbox.cpp 中实现）
// 用法：nlohmann::json j = message; 或 j.get<MailboxMessage>()
auto to_json(nlohmann::json& output, const MailboxMessage& msg) -> void;
auto from_json(const nlohmann::json& input, MailboxMessage& msg) -> void;

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Mailbox —— 文件系统消息队列的主类
//
// 设计模式：Pimpl（Pointer to Implementation）
//
// 为什么用 Pimpl？
//   1. 编译防火墙：私有成员（如 mutex）的改动不会触发包含 mailbox.h 的文件重编译。
//   2. 移动语义简单：只需要移动一个 unique_ptr，不需要手动移动每个成员。
//   3. 与 TaskManager 的设计保持一致——代码库内的一致性降低认知负担。
//
// 线程安全：
//   所有 public 方法内部通过 mutex 保护。对于「同一进程内的多线程并发 send」
//   是安全的。跨进程的并发由文件系统的 rename 原子性保证（见 send() 的注释）。
//
// 为什么用 poll() 而不是阻塞的 receive()？
//   - 阻塞 receive 需要 condition variable 或文件系统 watcher（Linux inotify、
//     Windows ReadDirectoryChangesW），复杂度较高。
//   - 当前设计是：Engine 的每一轮 agent loop 开始时调用 poll()，检查新消息。
//     这足够简单，且与「Engine 驱动所有 IO」的同步架构一致。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
class Mailbox
{
public:
    // 构造 Mailbox，指定根目录。
    // 典型值：default_codeharness_data_dir() / "mailboxes"
    // 构造时不创建根目录——只有在第一次 send 时才会创建。
    explicit Mailbox(std::filesystem::path root);

    ~Mailbox();

    // 禁止拷贝（因为内部有 mutex 和 unique_ptr）
    Mailbox(const Mailbox&) = delete;
    auto operator=(const Mailbox&) -> Mailbox& = delete;

    // 允许移动（Pimpl 模式下只需移动 unique_ptr）
    Mailbox(Mailbox&&) noexcept;
    auto operator=(Mailbox&&) noexcept -> Mailbox&;

    // 返回根目录路径（用于测试和调试）
    [[nodiscard]] auto root() const -> const std::filesystem::path&;

    // ─── send ───
    //
    // 向 recipient_id 的收件箱投递一条消息。
    //
    // 写入过程（原子写，与 tools/text_file.cpp 中的 atomic_write_text_file 相同）：
    //   1. 确保 inbox 目录存在
    //   2. 扫描 inbox，找到最大的序号
    //   3. 生成消息 ID 和时间戳，填入 message
    //   4. 将 JSON 写入 .tmp 文件
    //   5. flush 并关闭文件
    //   6. rename .tmp → 最终文件名（操作系统保证 rename 的原子性）
    //
    // 为什么要先写 .tmp 再 rename？
    //   如果进程在写入过程中崩溃，读者可能看到不完整的 JSON 文件，导致解析失败。
    //   rename 在 NTFS/ext4/APFS 上都是原子的——要么旧文件存在，要么新文件存在，
    //   不会出现「半截文件」的中间状态。
    //
    // 返回值：填充了 id、recipient_id、timestamp 的消息副本。
    auto send(const std::string& recipient_id, MailboxMessage message) -> Result<MailboxMessage>;

    // ─── poll ───
    //
    // 读取 task_id 收件箱中的消息。
    //
    // - unread_only = true 时，只返回 read 字段为 false 的消息。
    // - 返回的消息按文件名排序（即按发送时间排序）。
    // - 如果 inbox 目录不存在，返回空 vector（不是错误）。
    //
    // 注意：poll() 不会修改文件——消息的 read 状态需要通过 mark_read() 显式更新。
    auto poll(const std::string& task_id, bool unread_only = true) const -> Result<std::vector<MailboxMessage>>;

    // ─── mark_read ───
    //
    // 将指定消息标记为已读。
    // 原地更新 JSON 文件（同样是原子写：写 .tmp → rename）。
    auto mark_read(const std::string& task_id, const std::string& message_id) -> Result<void>;

    // ─── clear ───
    //
    // 清空 task_id 的收件箱：删除所有 .json 消息文件。
    // 忽略单个文件删除失败（例如文件已被其他进程删除）。
    auto clear(const std::string& task_id) -> Result<void>;

private:
    // Pimpl：私有实现细节隐藏在 .cpp 文件中
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 返回默认的 Mailbox 根目录路径。
// 即 default_codeharness_data_dir() / "mailboxes"。
// 如果无法确定 home 目录，返回 nullopt。
auto default_mailbox_root() -> std::optional<std::filesystem::path>;

} // namespace codeharness::mailbox
