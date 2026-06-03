// mailbox_tests.cpp —— Mailbox 和 SendMessageTool 的测试
//
// 测试策略：
//   每个测试用例专注于一个行为。测试之间相互独立，使用 TempDir 隔离。
//   遵循 project 中其他测试文件的风格（参见 task_tests.cpp）。
//
// 测试覆盖的维度：
//   1. 数据层：MailboxMessage 的 JSON 序列化/反序列化
//   2. 存储层：send、poll、mark_read、clear 的文件系统操作
//   3. 鲁棒性：崩溃恢复、损坏文件处理
//   4. 工具层：SendMessageTool 的输入解析、执行、权限目标

#include "test_support.h"

#include "codeharness/mailbox/mailbox.h"
#include "codeharness/mailbox/mailbox_tools.h"

// 匿名命名空间：测试辅助函数
namespace
{

// 创建一个简单的消息，用于测试。
// 参数使用默认值，调用方可以只关心想要测试的字段。
auto make_test_message(std::string sender = "sender-001", std::string recipient = "recipient-001",
                       std::string content = "hello") -> codeharness::mailbox::MailboxMessage
{
    codeharness::mailbox::MailboxMessage msg;
    msg.sender_id = std::move(sender);
    msg.recipient_id = std::move(recipient);
    msg.content = std::move(content);
    return msg;
}

} // namespace

// ─── 1. 数据层：JSON 序列化 ──────────────────────────────────────────────────────

// 【概念】序列化往返测试（Round-trip test）
//
// 这是最基础的数据正确性测试：将一个对象转为 JSON，再从 JSON 转回对象，
// 检查所有字段是否保持不变。如果序列化/反序列化有 bug，这里会首先暴露。
TEST_CASE("mailbox message serializes through nlohmann json interface")
{
    auto original = codeharness::mailbox::MailboxMessage{
        .id = "msg-a1b2c3d4",
        .type = codeharness::mailbox::MessageType::TaskResult,
        .sender_id = "sender-001",
        .recipient_id = "recipient-001",
        .content = "task completed successfully",
        .timestamp = "2026-06-03T14:30:00Z",
        .read = false,
    };

    // 序列化：C++ 对象 → JSON
    const nlohmann::json json = original;

    // 验证关键字段的 JSON 表示
    CHECK(json.at("id") == "msg-a1b2c3d4");
    CHECK(json.at("type") == "task_result");
    CHECK(json.at("sender_id") == "sender-001");
    CHECK(json.at("read") == false);

    // 反序列化：JSON → C++ 对象
    auto parsed = json.get<codeharness::mailbox::MailboxMessage>();

    // 逐字段验证
    CHECK(parsed.id == original.id);
    CHECK(parsed.type == original.type);
    CHECK(parsed.sender_id == original.sender_id);
    CHECK(parsed.recipient_id == original.recipient_id);
    CHECK(parsed.content == original.content);
    CHECK(parsed.timestamp == original.timestamp);
    CHECK(parsed.read == original.read);
}

// 验证 MessageType 枚举的字符串转换
TEST_CASE("message type converts to and from string")
{
    // 所有枚举值都应该能转为字符串再转回来
    for (auto type : {codeharness::mailbox::MessageType::UserMessage,
                      codeharness::mailbox::MessageType::TaskResult,
                      codeharness::mailbox::MessageType::PermissionRequest,
                      codeharness::mailbox::MessageType::PermissionResponse,
                      codeharness::mailbox::MessageType::Shutdown,
                      codeharness::mailbox::MessageType::IdleNotification})
    {
        auto name = codeharness::mailbox::message_type_name(type);
        CHECK(!name.empty());

        auto parsed = codeharness::mailbox::parse_message_type(name);
        REQUIRE(parsed.has_value());
        CHECK(*parsed == type);
    }

    // 未知的字符串应该返回错误
    auto unknown = codeharness::mailbox::parse_message_type("not_a_real_type");
    CHECK(!unknown.has_value());
}

// ─── 2. 存储层：send 操作 ────────────────────────────────────────────────────────

// 【概念】send 操作创建了文件
//
// 这验证了 send 的核心行为：调用 send 后，收件人的 inbox 目录中出现了消息文件。
TEST_CASE("mailbox send creates message file in recipient inbox")
{
    TempDir temp{"codeharness-mailbox-send-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    auto sent = mailbox.send("recipient-001", make_test_message());
    REQUIRE(sent.has_value());

    // 验证返回的消息填充了系统字段
    CHECK(!sent->id.empty());                          // 自动生成的消息 ID
    CHECK(sent->id.substr(0, 4) == "msg-");            // 格式：msg-XXXXXXXX
    CHECK(sent->recipient_id == "recipient-001");       // 填充了 recipient_id
    CHECK(!sent->timestamp.empty());                    // 自动生成的时间戳

    // 验证文件确实存在于磁盘上
    auto inbox = temp.path / "mailboxes" / "recipient-001" / "inbox";
    CHECK(std::filesystem::exists(inbox));

    // 收件箱中应该有恰好一个 .json 文件
    int file_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator{inbox})
    {
        if (entry.path().extension() == ".json")
        {
            file_count++;
        }
    }
    CHECK(file_count == 1);
}

// 【概念】序号文件名的递增
//
// 多次 send 应该产生递增的序号文件名（000001, 000002, ...），
// 而不是覆盖之前的消息。
TEST_CASE("mailbox send produces sequential filenames")
{
    TempDir temp{"codeharness-mailbox-seq-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    auto inbox = temp.path / "mailboxes" / "recipient-001" / "inbox";

    // 发送 3 条消息
    for (int i = 0; i < 3; ++i)
    {
        auto sent = mailbox.send("recipient-001", make_test_message("s", "r", "msg " + std::to_string(i)));
        REQUIRE(sent.has_value());
    }

    // 验证文件名
    CHECK(std::filesystem::exists(inbox / "000001.json"));
    CHECK(std::filesystem::exists(inbox / "000002.json"));
    CHECK(std::filesystem::exists(inbox / "000003.json"));
}

// 【概念】原子写入不留残余 .tmp 文件
//
// send 完成后，收件箱中不应该有 .tmp 文件。
// 如果有，说明原子写入过程中 rename 步骤没有正确执行。
TEST_CASE("mailbox send uses atomic write with no leftover tmp files")
{
    TempDir temp{"codeharness-mailbox-atomic-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    auto sent = mailbox.send("recipient-001", make_test_message());
    REQUIRE(sent.has_value());

    auto inbox = temp.path / "mailboxes" / "recipient-001" / "inbox";

    // 不应该有任何 .tmp 文件
    for (const auto& entry : std::filesystem::directory_iterator{inbox})
    {
        auto ext = entry.path().extension().string();
        CHECK(ext != ".tmp");
    }
}

// ─── 3. 存储层：poll 操作 ────────────────────────────────────────────────────────

// 【概念】poll 按顺序返回消息
//
// 多条消息应该按发送顺序（文件名序号顺序）返回，而不是随机的文件系统顺序。
TEST_CASE("mailbox poll returns messages in send order")
{
    TempDir temp{"codeharness-mailbox-poll-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    // 按顺序发送 3 条不同内容的消息
    REQUIRE(mailbox.send("recipient-001", make_test_message("s", "r", "first")).has_value());
    REQUIRE(mailbox.send("recipient-001", make_test_message("s", "r", "second")).has_value());
    REQUIRE(mailbox.send("recipient-001", make_test_message("s", "r", "third")).has_value());

    auto messages = mailbox.poll("recipient-001", false);
    REQUIRE(messages.has_value());
    REQUIRE(messages->size() == 3);

    // 验证顺序
    CHECK((*messages)[0].content == "first");
    CHECK((*messages)[1].content == "second");
    CHECK((*messages)[2].content == "third");
}

// 【概念】poll 的 unread_only 过滤
//
// 默认情况下，poll 只返回 read=false 的消息。
// 被 mark_read 标记过的消息不应该出现在默认的 poll 结果中。
TEST_CASE("mailbox poll filters unread only by default")
{
    TempDir temp{"codeharness-mailbox-unread-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    REQUIRE(mailbox.send("recipient-001", make_test_message("s", "r", "first")).has_value());
    auto second = mailbox.send("recipient-001", make_test_message("s", "r", "second"));
    REQUIRE(second.has_value());

    // 标记第二条消息为已读
    REQUIRE(mailbox.mark_read("recipient-001", second->id).has_value());

    // 默认 poll（unread_only=true）应该只返回第一条消息
    auto unread = mailbox.poll("recipient-001", true);
    REQUIRE(unread.has_value());
    REQUIRE(unread->size() == 1);
    CHECK(unread->front().content == "first");

    // poll（unread_only=false）应该返回两条消息
    auto all = mailbox.poll("recipient-001", false);
    REQUIRE(all.has_value());
    CHECK(all->size() == 2);
}

// poll 一个不存在的收件箱不应该报错，而应该返回空列表
TEST_CASE("mailbox poll returns empty for nonexistent inbox")
{
    TempDir temp{"codeharness-mailbox-empty-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    auto messages = mailbox.poll("nonexistent-task");
    REQUIRE(messages.has_value());
    CHECK(messages->empty());
}

// ─── 4. 存储层：mark_read 和 clear ──────────────────────────────────────────────

// 【概念】mark_read 更新磁盘上的消息文件
//
// 标记已读后，消息的 read 字段应该持久化到磁盘。
// 再次读取时应该看到 read=true。
TEST_CASE("mailbox mark_read updates message on disk")
{
    TempDir temp{"codeharness-mailbox-markread-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    auto sent = mailbox.send("recipient-001", make_test_message());
    REQUIRE(sent.has_value());

    // 标记已读
    auto result = mailbox.mark_read("recipient-001", sent->id);
    REQUIRE(result.has_value());

    // 验证：poll unread_only 应该返回空
    auto unread = mailbox.poll("recipient-001", true);
    REQUIRE(unread.has_value());
    CHECK(unread->empty());

    // 验证：poll all 应该包含该消息，且 read=true
    auto all = mailbox.poll("recipient-001", false);
    REQUIRE(all.has_value());
    REQUIRE(all->size() == 1);
    CHECK(all->front().read == true);
}

// 标记不存在的消息应该返回错误
TEST_CASE("mailbox mark_read returns error for missing message")
{
    TempDir temp{"codeharness-mailbox-markread-missing-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    // 创建 inbox（通过发送一条消息），然后尝试标记不存在的消息
    REQUIRE(mailbox.send("recipient-001", make_test_message()).has_value());

    auto result = mailbox.mark_read("recipient-001", "msg-nonexistent");
    CHECK(!result.has_value());
}

// mark_read 对不存在的收件箱也应该返回错误
TEST_CASE("mailbox mark_read returns error for nonexistent inbox")
{
    TempDir temp{"codeharness-mailbox-markread-noinbox-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    auto result = mailbox.mark_read("nonexistent-task", "msg-00000000");
    CHECK(!result.has_value());
}

// 【概念】clear 删除所有消息
//
// 清空后，poll 应该返回空列表。
TEST_CASE("mailbox clear removes all messages")
{
    TempDir temp{"codeharness-mailbox-clear-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    REQUIRE(mailbox.send("recipient-001", make_test_message("s", "r", "first")).has_value());
    REQUIRE(mailbox.send("recipient-001", make_test_message("s", "r", "second")).has_value());
    REQUIRE(mailbox.send("recipient-001", make_test_message("s", "r", "third")).has_value());

    auto result = mailbox.clear("recipient-001");
    REQUIRE(result.has_value());

    auto messages = mailbox.poll("recipient-001", false);
    REQUIRE(messages.has_value());
    CHECK(messages->empty());
}

// clear 一个不存在的收件箱不应该报错
TEST_CASE("mailbox clear succeeds for nonexistent inbox")
{
    TempDir temp{"codeharness-mailbox-clear-empty-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    auto result = mailbox.clear("nonexistent-task");
    CHECK(result.has_value());
}

// ─── 5. 鲁棒性：崩溃恢复和损坏处理 ──────────────────────────────────────────────

// 【概念】崩溃恢复测试
//
// 模拟进程重启：销毁 Mailbox 对象后，用相同的根目录创建新的 Mailbox。
// 新的 Mailbox 应该能读取之前写入的消息——这正是「文件系统持久化」的核心价值。
TEST_CASE("mailbox survives process restart via re-read from disk")
{
    TempDir temp{"codeharness-mailbox-restart-test"};

    // 第一个 Mailbox 实例：发送 3 条消息
    {
        codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};
        REQUIRE(mailbox.send("recipient-001", make_test_message("s", "r", "first")).has_value());
        REQUIRE(mailbox.send("recipient-001", make_test_message("s", "r", "second")).has_value());
        REQUIRE(mailbox.send("recipient-001", make_test_message("s", "r", "third")).has_value());
    }
    // Mailbox 对象被销毁——模拟进程退出

    // 第二个 Mailbox 实例：读取之前写入的消息
    {
        codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};
        auto messages = mailbox.poll("recipient-001", false);
        REQUIRE(messages.has_value());
        REQUIRE(messages->size() == 3);
        CHECK((*messages)[0].content == "first");
        CHECK((*messages)[1].content == "second");
        CHECK((*messages)[2].content == "third");
    }
}

// 【概念】损坏文件容错
//
// 如果一个消息文件的内容不是合法 JSON，poll 应该跳过它而不是报错。
// 这模拟了写入过程中进程崩溃导致的不完整文件。
TEST_CASE("mailbox poll skips corrupted message files")
{
    TempDir temp{"codeharness-mailbox-corrupt-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    // 正常发送一条消息
    REQUIRE(mailbox.send("recipient-001", make_test_message("s", "r", "valid message")).has_value());

    // 手动写入一个损坏的文件（模拟进程崩溃导致的不完整写入）
    auto inbox = temp.path / "mailboxes" / "recipient-001" / "inbox";
    auto corrupt_path = inbox / "000000.json";
    {
        std::ofstream file{corrupt_path, std::ios::binary};
        file << "this is not valid json {{{";
    }

    // poll 应该只返回有效消息，跳过损坏的
    auto messages = mailbox.poll("recipient-001", false);
    REQUIRE(messages.has_value());
    REQUIRE(messages->size() == 1);
    CHECK(messages->front().content == "valid message");
}

// ─── 6. 工具层：SendMessageTool ──────────────────────────────────────────────────

// 【概念】端到端工具测试
//
// 通过 ToolRegistry 执行 send_message 工具，验证从输入解析到文件写入的完整流程。
// 这测试了工具在 Engine agent loop 中被调用时的真实行为。
TEST_CASE("send_message tool sends message successfully")
{
    TempDir temp{"codeharness-sendmsg-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    codeharness::ToolRegistry registry;
    codeharness::mailbox::register_mailbox_tools(registry, mailbox, nullptr);

    codeharness::ToolContext context;
    context.cwd = temp.path;

    codeharness::ToolRequest request;
    request.id = "tool-use-001";
    request.name = "send_message";
    request.parsed_input = nlohmann::json{
        {"recipient_id", "task-worker-001"},
        {"content", "Please analyze the authentication module."},
        {"sender_id", "task-coordinator"},
    };

    // 执行工具
    auto response = registry.execute(request, context);
    REQUIRE(response.has_value());
    CHECK(!response->is_error);

    // 解析返回的 JSON
    const auto json = nlohmann::json::parse(response->content);
    CHECK(json.at("recipient_id") == "task-worker-001");
    CHECK(json.at("content") == "Please analyze the authentication module.");
    CHECK(json.at("sender_id") == "task-coordinator");
    CHECK(!json.at("id").get<std::string>().empty());
    CHECK(!json.at("timestamp").get<std::string>().empty());

    // 验证消息确实写入了磁盘
    auto messages = mailbox.poll("task-worker-001", true);
    REQUIRE(messages.has_value());
    REQUIRE(messages->size() == 1);
    CHECK(messages->front().content == "Please analyze the authentication module.");
}

// 【概念】输入验证测试
//
// 缺少必填字段时，工具应该返回清晰的错误信息。
// 这对 LLM 来说很重要——清晰的错误帮助它修正下一次调用。
TEST_CASE("send_message tool validates required fields")
{
    TempDir temp{"codeharness-sendmsg-validation-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    codeharness::ToolRegistry registry;
    codeharness::mailbox::register_mailbox_tools(registry, mailbox, nullptr);

    codeharness::ToolContext context;
    context.cwd = temp.path;

    // 缺少 recipient_id
    {
        codeharness::ToolRequest request;
        request.id = "tool-use-missing-recipient";
        request.name = "send_message";
        request.parsed_input = nlohmann::json{{"content", "hello"}};

        auto response = registry.execute(request, context);
        CHECK(!response.has_value());
    }

    // 缺少 content
    {
        codeharness::ToolRequest request;
        request.id = "tool-use-missing-content";
        request.name = "send_message";
        request.parsed_input = nlohmann::json{{"recipient_id", "task-001"}};

        auto response = registry.execute(request, context);
        CHECK(!response.has_value());
    }
}

// 【概念】收件人存在性验证
//
// 当提供了 TaskManager 时，send_message 工具会验证收件人是否存在。
// 这给 LLM 提供了更友好的错误信息（"收件人不存在"而非静默发送到无人的邮箱）。
TEST_CASE("send_message tool validates recipient exists via task_manager")
{
    TempDir temp{"codeharness-sendmsg-tm-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};
    codeharness::tasks::TaskManager task_manager{temp.path / "tasks"};

    codeharness::ToolRegistry registry;
    codeharness::mailbox::register_mailbox_tools(registry, mailbox, &task_manager);

    codeharness::ToolContext context;
    context.cwd = temp.path;

    codeharness::ToolRequest request;
    request.id = "tool-use-bad-recipient";
    request.name = "send_message";
    request.parsed_input = nlohmann::json{
        {"recipient_id", "nonexistent-task"},
        {"content", "hello"},
    };

    auto response = registry.execute(request, context);
    REQUIRE(response.has_value());
    CHECK(response->is_error);
    CHECK(response->content.find("not found") != std::string::npos);
}

// 【概念】权限目标测试
//
// 验证 permission_target 返回正确的结构，包含收件人 ID。
// 这确保权限系统能够正确评估 send_message 的权限。
TEST_CASE("send_message tool permission target includes recipient")
{
    TempDir temp{"codeharness-sendmsg-perm-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    codeharness::ToolRegistry registry;
    codeharness::mailbox::register_mailbox_tools(registry, mailbox, nullptr);

    const auto* tool = registry.find("send_message");
    REQUIRE(tool != nullptr);

    codeharness::ToolRequest request;
    request.id = "perm-check";
    request.name = "send_message";
    request.parsed_input = nlohmann::json{
        {"recipient_id", "task-worker-001"},
        {"content", "hello"},
    };

    auto target = tool->permission_target(request);
    REQUIRE(target.command.has_value());
    CHECK(target.command->find("send_message") != std::string::npos);
    CHECK(target.command->find("task-worker-001") != std::string::npos);
}

// 验证 send_message 工具的元信息
TEST_CASE("send_message tool has correct metadata")
{
    TempDir temp{"codeharness-sendmsg-meta-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    codeharness::ToolRegistry registry;
    codeharness::mailbox::register_mailbox_tools(registry, mailbox, nullptr);

    const auto* tool = registry.find("send_message");
    REQUIRE(tool != nullptr);
    CHECK(tool->name() == "send_message");
    CHECK(!tool->description().empty());
    CHECK(tool->is_read_only() == false);
}

// 验证发送不同类型的消息
TEST_CASE("send_message tool sends different message types")
{
    TempDir temp{"codeharness-sendmsg-types-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    codeharness::ToolRegistry registry;
    codeharness::mailbox::register_mailbox_tools(registry, mailbox, nullptr);

    codeharness::ToolContext context;
    context.cwd = temp.path;

    // 发送 shutdown 消息
    codeharness::ToolRequest request;
    request.id = "tool-use-shutdown";
    request.name = "send_message";
    request.parsed_input = nlohmann::json{
        {"recipient_id", "task-worker-001"},
        {"content", "Stop working immediately."},
        {"type", "shutdown"},
    };

    auto response = registry.execute(request, context);
    REQUIRE(response.has_value());
    CHECK(!response->is_error);

    const auto json = nlohmann::json::parse(response->content);
    CHECK(json.at("type") == "shutdown");

    // 验证磁盘上的消息也是 shutdown 类型
    auto messages = mailbox.poll("task-worker-001", true);
    REQUIRE(messages.has_value());
    REQUIRE(messages->size() == 1);
    CHECK(messages->front().type == codeharness::mailbox::MessageType::Shutdown);
}
