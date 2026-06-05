#include "codeharness/mailbox/mailbox.h"

#include "codeharness/core/json_parse.h"
#include "codeharness/core/paths.h"
#include "codeharness/core/time.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <system_error>
#include <ranges>

namespace codeharness::mailbox
{

auto message_type_name(MessageType type) -> std::string_view
{
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
    
    [[unreachable]] return "unknown";
}

auto parse_message_type(std::string_view value) -> Result<MessageType>
{
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
    msg.id = input.at("id").get<std::string>();

    const auto type_str = input.at("type").get<std::string>();
    auto type_result = parse_message_type(type_str);
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

// default_mailbox_root —— 默认的 Mailbox 根目录
//
// 返回 ~/.codeharness/data/mailboxes
auto default_mailbox_root() -> std::optional<std::filesystem::path>
{
    const auto data_dir = default_codeharness_data_dir();
    if (!data_dir)
    {
        return std::nullopt;
    }
    return *data_dir / "mailboxes";
}

namespace
{

// TODO:UUID
// 生成唯一消息 ID："msg-" 前缀 + 8 位十六进制随机数。
auto generate_message_id() -> std::string
{
    std::random_device rd;
    auto value = rd();

    // 将 32 位随机数转为 8 位十六进制字符串
    return std::format("msg-{0:08x}", value);
}

// 遍历 inbox 目录，返回所有有效的 .json 文件路径。
auto inbox_json_files(const std::filesystem::path& inbox, std::error_code& error)
    -> std::vector<std::filesystem::path>
{
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator{inbox, error})
    {
        if (!entry.is_regular_file()) { continue; }
        if (entry.path().extension() != ".json") { continue; }
        files.push_back(entry.path());
    }
    return files;
}

// 构造收件箱目录路径：root / task_id / "inbox"
auto inbox_path(const std::filesystem::path& root, std::string_view task_id) -> std::filesystem::path
{
    return root / task_id / "inbox";
}

// 扫描 inbox 目录，返回下一个可用的序号文件名。
//
// 算法：遍历目录中所有 *.json 文件，提取文件名中的数字部分，
//       找到最大值，加 1，格式化为 6 位数字 + ".json"。
//       如果目录为空或不存在，从 1 开始。
auto next_sequence_file(const std::filesystem::path& inbox) -> std::string
{
    int max_seq = 0;

    std::error_code error;
    for (const auto& path : inbox_json_files(inbox, error))
    {
        // 提取文件名中的数字部分：000001.json → "000001"
        auto num_str = path.stem().string();

        int seq = 0;
        auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), seq);
        if (ec == std::errc{} && seq > max_seq)
        {
            max_seq = seq;
        }
    }

    // 格式化为 6 位数字，前导零填充
    // 7 → "000007.json"
    return std::format("{0:06d}.json", max_seq + 1);
}

// 原子写入文本文件。
//
// 写入步骤：
//   1. 写入 target_path + ".tmp"
//   2. flush（确保数据从用户空间缓冲区写入内核缓冲区）
//   3. 关闭文件
//   4. rename .tmp → 最终路径
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
        return std::nullopt;
    }
}

} // namespace


struct Mailbox::Impl
{
    explicit Impl(std::filesystem::path root_path)
        : root{std::move(root_path)}
    {
    }

    std::filesystem::path root;
    mutable std::mutex mutex;
};

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

// Mailbox::send —— 投递消息
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
// 锁的作用范围是同一进程内的线程安全。
// 跨进程的并发安全由文件系统的 rename 原子性保证。
// 如果两个独立进程同时向同一个 inbox 写入，可能产生序号冲突，
// 但在实际使用中，每个 inbox 通常只有一个写入者（coordinator），
auto Mailbox::send(const std::string& recipient_id, MailboxMessage message) -> Result<MailboxMessage>
{
    const std::scoped_lock lock{impl_->mutex};

    // 确定收件箱目录
    const auto inbox = inbox_path(impl_->root, recipient_id);

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

    // 序列化为 JSON
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

auto Mailbox::poll(const std::string& task_id, bool unread_only) const -> Result<std::vector<MailboxMessage>>
{
    const auto inbox = inbox_path(impl_->root, task_id);

    // 如果收件箱目录不存在，返回空列表
    std::error_code error;
    if (!std::filesystem::exists(inbox, error))
    {
        return std::vector<MailboxMessage>{};
    }

    // 收集所有pair<文件名, 消息>
    std::vector<std::pair<std::string, MailboxMessage>> messages;

    for (const auto& path : inbox_json_files(inbox, error))
    {
        // 读取并解析消息文件
        auto msg = read_message_file(path);
        if (!msg)
        {
            // 文件损坏——跳过
            continue;
        }

        // 如果只要未读消息，跳过已读的
        if (unread_only && msg->read)
        {
            continue;
        }

        messages.emplace_back(path.filename().string(), std::move(*msg));
    }

    // 按文件名排序
    std::ranges::sort(messages, [](const auto& a, const auto& b) { return a.first < b.first; });

    auto view = messages | std::views::transform([](auto& p) -> MailboxMessage&& { return std::move(p.second); });
    return std::vector<MailboxMessage>{view.begin(), view.end()};
}

auto Mailbox::mark_read(const std::string& task_id, const std::string& message_id) -> Result<void>
{
    std::scoped_lock lock{impl_->mutex};

    const auto inbox = inbox_path(impl_->root, task_id);

    std::error_code error;
    if (!std::filesystem::exists(inbox, error))
    {
        return fail<void>(ErrorKind::InvalidArgument, "mailbox: inbox not found for task: " + task_id);
    }

    // 遍历收件箱，查找目标消息
    for (const auto& path : inbox_json_files(inbox, error))
    {
        auto msg = read_message_file(path);
        if (!msg)
        {
            continue;
        }

        if (msg->id == message_id)
        {
            // 找到目标消息——更新 read 字段并原子重写
            msg->read = true;
            const nlohmann::json json = *msg;
            return atomic_write_file(path, json.dump(2));
        }
    }

    return fail<void>(ErrorKind::InvalidArgument, "mailbox: message not found: " + message_id);
}

auto Mailbox::clear(const std::string& task_id) -> Result<void>
{
    std::scoped_lock lock{impl_->mutex};

    const auto inbox = inbox_path(impl_->root, task_id);

    std::error_code error;
    if (!std::filesystem::exists(inbox, error))
    {
        return {};
    }

    for (const auto& path : inbox_json_files(inbox, error))
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    return {};
}

} // namespace codeharness::mailbox
