// mailbox.cpp — Mailbox 文件系统消息队列的实现
//
// 本文件实现了 mailbox.h 中声明的所有类和函数。
// 核心设计思路见 mailbox.h 的文件头注释。

#include "codeharness/mailbox/mailbox.h"

#include "codeharness/core/json_parse.h"
#include "codeharness/core/paths.h"
#include "codeharness/core/time.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <system_error>

namespace codeharness::mailbox
{

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// MessageType ↔ 字符串转换
//
// 为什么需要这些转换函数？
//   C++ 内部使用 enum class（类型安全），但 JSON 是文本格式（字符串）。
//   这两个函数是「类型安全的 C++ 世界」和「文本格式的 JSON 世界」之间的桥梁。
//   所有字符串<->枚举的转换逻辑都集中在这里，避免散落在代码各处。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

auto message_type_name(MessageType type) -> std::string_view
{
    // 使用 switch 而不是查找表，因为编译器会检查是否覆盖了所有枚举值。
    // 如果将来新增枚举值但忘记更新这里，编译器会发出警告。
    switch (type)
    {
    case MessageType::UserMessage:
        return "user_message";
    case MessageType::TaskResult:
        return "task_result";
    case MessageType::PermissionRequest:
        return "permission_request";
    case MessageType::PermissionResponse:
        return "permission_response";
    case MessageType::Shutdown:
        return "shutdown";
    case MessageType::IdleNotification:
        return "idle_notification";
    }
    // 不应该到达这里——所有枚举值都已处理。
    // 如果编译器新增了枚举值但上面没有对应的 case，这是安全的兜底。
    return "unknown";
}

auto parse_message_type(std::string_view value) -> Result<MessageType>
{
    // 字符串 → 枚举。不区分大小写可以提高容错性，但这里的 JSON 是我们自己写的，
    // 所以用精确匹配。如果遇到未知的字符串，返回错误。
    if (value == "user_message")
    {
        return MessageType::UserMessage;
    }
    if (value == "task_result")
    {
        return MessageType::TaskResult;
    }
    if (value == "permission_request")
    {
        return MessageType::PermissionRequest;
    }
    if (value == "permission_response")
    {
        return MessageType::PermissionResponse;
    }
    if (value == "shutdown")
    {
        return MessageType::Shutdown;
    }
    if (value == "idle_notification")
    {
        return MessageType::IdleNotification;
    }

    return fail<MessageType>(ErrorKind::InvalidArgument, "unknown message type: " + std::string{value});
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// MailboxMessage 的 JSON 序列化
//
// 使用 nlohmann/json 的 ADL（Argument-Dependent Lookup）机制。
// 定义 to_json/from_json 自由函数后，可以自然地写：
//   nlohmann::json j = message;       // 调用 to_json
//   message = j.get<MailboxMessage>(); // 调用 from_json
//
// 这与 TaskRecord 在 task_manager.cpp 中的序列化方式完全一致。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

auto to_json(nlohmann::json& output, const MailboxMessage& msg) -> void
{
    output = nlohmann::json{
        {"id", msg.id},
        {"type", message_type_name(msg.type)},
        {"sender_id", msg.sender_id},
        {"recipient_id", msg.recipient_id},
        {"content", msg.content},
        {"timestamp", msg.timestamp},
        {"read", msg.read},
    };
}

auto from_json(const nlohmann::json& input, MailboxMessage& msg) -> void
{
    // 使用 at() 而不是 [] —— at() 在字段不存在时会抛异常，
    // 这会被 nlohmann/json 的 get<T>() 机制捕获并转化为有意义的错误信息。
    msg.id = input.at("id").get<std::string>();

    const auto type_str = input.at("type").get<std::string>();
    auto type_result = parse_message_type(type_str);
    // 如果 JSON 中的 type 字段无法识别，nlohmann 会抛出异常，这里用 throw 让它传播。
    if (!type_result)
    {
        throw std::runtime_error{"invalid message type: " + type_str};
    }
    msg.type = *type_result;

    msg.sender_id = input.at("sender_id").get<std::string>();
    msg.recipient_id = input.at("recipient_id").get<std::string>();
    msg.content = input.at("content").get<std::string>();
    msg.timestamp = input.at("timestamp").get<std::string>();
    msg.read = input.at("read").get<bool>();
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// default_mailbox_root —— 默认的 Mailbox 根目录
//
// 返回 ~/.codeharness/data/mailboxes
// 如果无法确定 home 目录（极其罕见），返回 nullopt。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

auto default_mailbox_root() -> std::optional<std::filesystem::path>
{
    const auto data_dir = default_codeharness_data_dir();
    if (!data_dir)
    {
        return std::nullopt;
    }
    return *data_dir / "mailboxes";
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// 匿名命名空间：仅在本文件内可见的辅助函数
//
// 将内部实现细节放在匿名命名空间中，相当于给它们加上了 static 链接属性——
// 其他 .cpp 文件无法看到或调用这些函数。这避免了符号冲突，也向读者明确表示
// 「这些是内部实现细节，不是公共 API」。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
namespace
{

// 生成唯一消息 ID："msg-" 前缀 + 8 位十六进制随机数。
//
// 为什么用 std::random_device 而不是 rand()？
//   - rand() 的随机性质量差，且多线程使用时需要手动管理 seed。
//   - std::random_device 使用操作系统的真随机数源（如 /dev/urandom），
//     生成不可预测的 ID，适合用于标识符。
//
// 为什么是 8 位十六进制？
//   - 4 字节 = 32 位 → 2^32 ≈ 42 亿种可能。对于消息 ID 来说足够了。
//   - 简洁，人类可以一眼识别。
//   - 碰撞概率极低：发送 10 万条消息后碰撞概率 < 0.001%。
auto generate_message_id() -> std::string
{
    // random_device 在某些旧 Linux 实现上可能返回伪随机数，
    // 但在现代系统（Linux 3.17+/Windows）上都是真随机数源。
    std::random_device rd;
    auto value = rd();

    // 将 32 位随机数转为 8 位十六进制字符串
    // 格式化说明：%08x → 8 位宽，前导零填充，十六进制小写
    char buf[16];
    std::snprintf(buf, sizeof(buf), "msg-%08x", value);
    return buf;
}

// 构造收件箱目录路径：root / task_id / "inbox"
//
// 为什么是函数而不是硬编码路径拼接？
//   路径构造逻辑集中在一处，如果将来目录结构变化，只需修改这里。
auto inbox_path(const std::filesystem::path& root, const std::string& task_id) -> std::filesystem::path
{
    return root / task_id / "inbox";
}

// 扫描 inbox 目录，返回下一个可用的序号文件名。
//
// 算法：遍历目录中所有 *.json 文件，提取文件名中的数字部分，
//       找到最大值，加 1，格式化为 6 位数字 + ".json"。
//       如果目录为空或不存在，从 1 开始。
//
// 为什么扫描而不是维护一个计数器变量？
//   - 计数器变量在进程重启后会丢失。扫描文件系统可以正确恢复序号。
//   - 消息数量通常很少（几十到几百条），扫描的开销可以忽略不计。
//   - 如果将来消息量增长到数万条，可以引入 metadata 文件缓存当前序号。
auto next_sequence_file(const std::filesystem::path& inbox) -> std::string
{
    int max_seq = 0;

    std::error_code error;
    // 使用带 error_code 的重载，避免目录不存在时抛异常
    for (const auto& entry : std::filesystem::directory_iterator{inbox, error})
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        auto filename = entry.path().filename().string();

        // 只关心 .json 文件，跳过 .tmp 和其他文件
        // 这样即使有残留的 .tmp 文件也不会影响序号计数
        if (filename.size() < 11 || filename.substr(filename.size() - 5) != ".json")
        {
            continue;
        }

        // 提取文件名中的数字部分：000001.json → "000001"
        auto num_str = filename.substr(0, filename.size() - 5);

        // 尝试将数字部分转为整数
        // 如果文件名格式异常（不是纯数字），跳过这个文件
        try
        {
            int seq = std::stoi(num_str);
            if (seq > max_seq)
            {
                max_seq = seq;
            }
        }
        catch (const std::exception&)
        {
            // 文件名不是数字格式——跳过，不中断处理
        }
    }

    // 格式化为 6 位数字，前导零填充
    // 例如：7 → "000007.json"
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%06d.json", max_seq + 1);
    return buf;
}

// 原子写入文本文件。
//
// 这个函数复刻了 tools/text_file.cpp 中 atomic_write_text_file 的逻辑。
// 为什么不直接调用 text_file.h 中的函数？
//   - text_file.h 中的函数是 tools 模块的一部分，mailbox 不应该依赖 tools 模块
//     （依赖方向应该是 tools → foundation，而不是 mailbox → tools）。
//   - 保持 mailbox 模块的独立性，只依赖 foundation 层。
//
// 写入步骤：
//   1. 写入 target_path + ".tmp"
//   2. flush（确保数据从用户空间缓冲区写入内核缓冲区）
//   3. 关闭文件
//   4. rename .tmp → 最终路径（操作系统保证 rename 的原子性）
auto atomic_write_file(const std::filesystem::path& target_path, std::string_view content) -> Result<void>
{
    auto tmp_path = target_path;
    tmp_path += ".tmp";

    // binary 模式：避免 Windows 上的 \n → \r\n 转换
    std::ofstream file{tmp_path, std::ios::binary};
    if (!file)
    {
        return fail<void>(ErrorKind::Io, "mailbox: failed to create temp file: " + tmp_path.string());
    }

    file << content;
    file.flush();

    if (!file.good())
    {
        // 写入失败，清理临时文件
        std::error_code ignored;
        std::filesystem::remove(tmp_path, ignored);
        return fail<void>(ErrorKind::Io, "mailbox: failed to write message content");
    }

    file.close();

    // rename 是原子的：操作系统保证要么旧文件存在，要么新文件存在
    // 在 Windows (NTFS) 上，MoveFileExW 在同一卷内是原子的
    // 在 Linux (ext4) 上，rename(2) 是原子的
    std::error_code rename_error;
    std::filesystem::rename(tmp_path, target_path, rename_error);
    if (rename_error)
    {
        std::error_code ignored;
        std::filesystem::remove(tmp_path, ignored);
        return fail<void>(ErrorKind::Io, "mailbox: failed to rename temp file: " + rename_error.message());
    }

    return {};
}

// 读取一个 JSON 文件并解析为 MailboxMessage。
//
// 如果文件内容不是合法的 JSON 或不符合 MailboxMessage 的结构，
// 返回 nullopt（不传播异常），让调用方可以跳过损坏的文件继续处理。
// 这是「尽力而为」的策略：一条损坏的消息不应阻断整个收件箱的读取。
auto read_message_file(const std::filesystem::path& path) -> std::optional<MailboxMessage>
{
    std::ifstream file{path, std::ios::binary};
    if (!file)
    {
        return std::nullopt;
    }

    try
    {
        nlohmann::json json;
        file >> json;
        return json.get<MailboxMessage>();
    }
    catch (const std::exception&)
    {
        // 文件内容损坏或格式不符——跳过这条消息
        // 在实际运行中，这可能是因为写入过程中进程崩溃导致的不完整 JSON
        return std::nullopt;
    }
}

} // namespace

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Mailbox::Impl —— 私有实现（Pimpl 模式）
//
// 为什么把成员变量放在 Impl 中而不是直接放在 Mailbox 中？
//   这是 Pimpl（Pointer to Implementation）模式：
//   - 头文件（mailbox.h）的用户不需要知道 mutex、路径等实现细节。
//   - 修改 Impl（如添加新成员）不会导致包含 mailbox.h 的文件重新编译。
//   - Mailbox 类本身只需要一个 unique_ptr<Impl>，移动操作变得简单。
//
//   这与 TaskManager 的设计完全一致。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
struct Mailbox::Impl
{
    explicit Impl(std::filesystem::path root_path)
        : root{std::move(root_path)}
    {
    }

    std::filesystem::path root;

    // 互斥锁：保护 send() 和 mark_read() 中的文件写入操作。
    //
    // 为什么需要互斥锁？
    //   同一进程内可能有多个线程同时调用 send()。虽然文件系统的 rename 是原子的，
    //   但「扫描最大序号」和「写入新文件」这两步之间不是原子的。
    //   如果两个线程同时扫描到 max_seq = 5，它们会试图创建同一个文件名 000006.json。
    //   mutex 确保「扫描 + 写入」作为一个整体不会被并发打断。
    //
    // 为什么是 mutable？
    //   poll() 是 const 方法，但它需要读文件系统（不需要锁）。
    //   如果将来 poll() 也需要锁保护（例如缓存读取结果），mutable 允许在 const
    //   方法中使用。目前 mutable 只是为了保持一致性。
    mutable std::mutex mutex;
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Mailbox 的构造/析构/移动
//
// 析构和移动不需要手动清理文件——消息持久化在磁盘上，Mailbox 对象的生命周期
// 与消息的生命周期无关。这正是文件系统队列的核心优势之一。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Mailbox::Mailbox(std::filesystem::path root)
    : impl_{std::make_unique<Impl>(std::move(root))}
{
}

Mailbox::~Mailbox() = default;

Mailbox::Mailbox(Mailbox&&) noexcept = default;

auto Mailbox::operator=(Mailbox&&) noexcept -> Mailbox& = default;

auto Mailbox::root() const -> const std::filesystem::path&
{
    return impl_->root;
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Mailbox::send —— 投递消息
//
// 这是最核心的方法。它的工作流程是：
//
//   1. 加锁（保护「扫描序号 + 写入」的原子性）
//   2. 构造收件箱路径：root / recipient_id / inbox
//   3. 确保目录存在
//   4. 扫描目录，确定下一个序号文件名
//   5. 填充消息的 ID、recipient_id、timestamp
//   6. 序列化为 JSON
//   7. 原子写入（.tmp → rename）
//   8. 返回完整的消息（调用方可以知道生成的 ID 和时间戳）
//
// 注意：锁的作用范围是同一进程内的线程安全。
// 跨进程的并发安全由文件系统的 rename 原子性保证。
// 如果两个独立进程同时向同一个 inbox 写入，理论上可能产生序号冲突，
// 但在实际使用中，每个 inbox 通常只有一个写入者（coordinator），
// 所以这不是当前需要解决的问题。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
auto Mailbox::send(const std::string& recipient_id, MailboxMessage message) -> Result<MailboxMessage>
{
    // RAII 锁：离开作用域时自动释放
    const std::scoped_lock lock{impl_->mutex};

    // 确定收件箱目录
    const auto inbox = inbox_path(impl_->root, recipient_id);

    // 确保目录存在。如果目录已存在，create_directories 什么都不做。
    auto dir_result = ensure_directory(inbox, "mailbox inbox");
    if (!dir_result)
    {
        return nonstd::make_unexpected(dir_result.error());
    }

    // 获取下一个可用的序号文件名
    const auto filename = next_sequence_file(inbox);

    // 填充消息的系统字段
    message.id = generate_message_id();
    message.recipient_id = recipient_id;
    message.timestamp = utc_timestamp_seconds();

    // 序列化为 JSON（格式化输出，方便人工调试时阅读）
    const nlohmann::json json = message;
    const auto content = json.dump(2);

    // 原子写入到磁盘
    const auto file_path = inbox / filename;
    auto write_result = atomic_write_file(file_path, content);
    if (!write_result)
    {
        return nonstd::make_unexpected(write_result.error());
    }

    return message;
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Mailbox::poll —— 读取收件箱中的消息
//
// 为什么是 const 方法？
//   poll() 只读取文件系统，不修改 Mailbox 的内部状态，也不修改磁盘上的文件。
//   消息的 read 状态由 mark_read() 独立更新，poll() 只是过滤——这是一种
//   「查询与命令分离」（CQS）的设计。
//
// 为什么返回 vector 而不是迭代器？
//   消息数量通常很少（几十条），vector 的开销可以忽略。
//   返回 vector 更简单，调用方可以直接使用，不需要理解迭代器协议。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
auto Mailbox::poll(const std::string& task_id, bool unread_only) const -> Result<std::vector<MailboxMessage>>
{
    const auto inbox = inbox_path(impl_->root, task_id);

    // 如果收件箱目录不存在，返回空列表——这不是错误，只是没有消息。
    // 这与「打开一个空文件」的语义类似：没有内容，但不是失败。
    std::error_code error;
    if (!std::filesystem::exists(inbox, error))
    {
        return std::vector<MailboxMessage>{};
    }

    // 收集所有消息和对应的文件名
    // 使用 pair<文件名, 消息> 方便后续排序
    std::vector<std::pair<std::string, MailboxMessage>> messages;

    for (const auto& entry : std::filesystem::directory_iterator{inbox, error})
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        auto filename = entry.path().filename().string();

        // 只读取 .json 文件，跳过 .tmp 和其他文件
        if (filename.size() < 6 || filename.substr(filename.size() - 5) != ".json")
        {
            continue;
        }

        // 读取并解析消息文件
        auto msg = read_message_file(entry.path());
        if (!msg)
        {
            // 文件损坏——跳过，继续处理其他消息
            // 这种「尽力而为」的策略确保一条坏消息不会阻断整个收件箱
            continue;
        }

        // 如果只要未读消息，跳过已读的
        if (unread_only && msg->read)
        {
            continue;
        }

        messages.emplace_back(std::move(filename), std::move(*msg));
    }

    // 按文件名排序——因为文件名是 000001、000002 格式，
    // 字典序排序自然就是时间序排序
    std::sort(messages.begin(), messages.end(),
              [](const auto& a, const auto& b)
              { return a.first < b.first; });

    // 提取消息，丢弃文件名
    std::vector<MailboxMessage> result;
    result.reserve(messages.size());
    for (auto& [_, msg] : messages)
    {
        result.push_back(std::move(msg));
    }

    return result;
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Mailbox::mark_read —— 将消息标记为已读
//
// 为什么需要显式标记已读，而不是在 poll 时自动标记？
//   因为 poll 可能被多次调用（例如重试时），如果自动标记已读，
//   第二次 poll 就看不到这条消息了。显式标记让调用方有完全的控制权。
//
// 实现方式：找到目标消息文件 → 修改 read 字段 → 原子重写整个文件
// 为什么重写整个文件而不是原地修改？
//   JSON 文件不支持原地修改字段值（修改可能改变文件大小，导致覆盖后续内容）。
//   重写是安全的做法，而且文件通常很小（几百字节），开销可以忽略。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
auto Mailbox::mark_read(const std::string& task_id, const std::string& message_id) -> Result<void>
{
    const std::scoped_lock lock{impl_->mutex};

    const auto inbox = inbox_path(impl_->root, task_id);

    std::error_code error;
    if (!std::filesystem::exists(inbox, error))
    {
        return fail<void>(ErrorKind::InvalidArgument, "mailbox: inbox not found for task: " + task_id);
    }

    // 遍历收件箱，查找目标消息
    for (const auto& entry : std::filesystem::directory_iterator{inbox, error})
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        auto filename = entry.path().filename().string();
        if (filename.size() < 6 || filename.substr(filename.size() - 5) != ".json")
        {
            continue;
        }

        auto msg = read_message_file(entry.path());
        if (!msg)
        {
            continue;
        }

        if (msg->id == message_id)
        {
            // 找到目标消息——更新 read 字段并原子重写
            msg->read = true;
            const nlohmann::json json = *msg;
            return atomic_write_file(entry.path(), json.dump(2));
        }
    }

    return fail<void>(ErrorKind::InvalidArgument, "mailbox: message not found: " + message_id);
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Mailbox::clear —— 清空收件箱
//
// 删除 inbox 目录下的所有 .json 文件。
// 忽略单个文件删除失败——可能是因为另一个进程已经删除了它。
// 这是「最终一致」的策略：只要最终结果是空收件箱，中间状态不影响正确性。
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
auto Mailbox::clear(const std::string& task_id) -> Result<void>
{
    const std::scoped_lock lock{impl_->mutex};

    const auto inbox = inbox_path(impl_->root, task_id);

    std::error_code error;
    if (!std::filesystem::exists(inbox, error))
    {
        // 目录不存在——等价于空收件箱，直接返回成功
        return {};
    }

    for (const auto& entry : std::filesystem::directory_iterator{inbox, error})
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        auto filename = entry.path().filename().string();
        if (filename.size() < 6 || filename.substr(filename.size() - 5) != ".json")
        {
            continue;
        }

        // 忽略删除失败——另一个进程可能已经删除了这个文件
        std::error_code ignored;
        std::filesystem::remove(entry.path(), ignored);
    }

    return {};
}

} // namespace codeharness::mailbox
