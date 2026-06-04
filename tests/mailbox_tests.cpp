#include "test_support.h"

#include "codeharness/mailbox/mailbox.h"
#include "codeharness/mailbox/mailbox_tools.h"
#include "codeharness/mailbox/message_consumer.h"

// 匿名命名空间：测试辅助函数
namespace
{

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

    const nlohmann::json json = original;

    // 验证关键字段的 JSON 表示
    CHECK(json.at("id") == "msg-a1b2c3d4");
    CHECK(json.at("type") == "task_result");
    CHECK(json.at("sender_id") == "sender-001");
    CHECK(json.at("read") == false);

    auto parsed = json.get<codeharness::mailbox::MailboxMessage>();

    CHECK(parsed.id == original.id);
    CHECK(parsed.type == original.type);
    CHECK(parsed.sender_id == original.sender_id);
    CHECK(parsed.recipient_id == original.recipient_id);
    CHECK(parsed.content == original.content);
    CHECK(parsed.timestamp == original.timestamp);
    CHECK(parsed.read == original.read);
}

TEST_CASE("message type converts to and from string")
{
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

    auto unknown = codeharness::mailbox::parse_message_type("not_a_real_type");
    CHECK(!unknown.has_value());
}

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

TEST_CASE("mailbox send produces sequential filenames")
{
    TempDir temp{"codeharness-mailbox-seq-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    auto inbox = temp.path / "mailboxes" / "recipient-001" / "inbox";

    for (int i = 0; i < 3; ++i)
    {
        auto sent = mailbox.send("recipient-001", make_test_message("s", "r", "msg " + std::to_string(i)));
        REQUIRE(sent.has_value());
    }

    CHECK(std::filesystem::exists(inbox / "000001.json"));
    CHECK(std::filesystem::exists(inbox / "000002.json"));
    CHECK(std::filesystem::exists(inbox / "000003.json"));
}

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

TEST_CASE("mailbox poll returns empty for nonexistent inbox")
{
    TempDir temp{"codeharness-mailbox-empty-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    auto messages = mailbox.poll("nonexistent-task");
    REQUIRE(messages.has_value());
    CHECK(messages->empty());
}

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

TEST_CASE("mailbox mark_read returns error for missing message")
{
    TempDir temp{"codeharness-mailbox-markread-missing-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};
    REQUIRE(mailbox.send("recipient-001", make_test_message()).has_value());

    auto result = mailbox.mark_read("recipient-001", "msg-nonexistent");
    CHECK(!result.has_value());
}

TEST_CASE("mailbox mark_read returns error for nonexistent inbox")
{
    TempDir temp{"codeharness-mailbox-markread-noinbox-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    auto result = mailbox.mark_read("nonexistent-task", "msg-00000000");
    CHECK(!result.has_value());
}

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

TEST_CASE("mailbox clear succeeds for nonexistent inbox")
{
    TempDir temp{"codeharness-mailbox-clear-empty-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    auto result = mailbox.clear("nonexistent-task");
    CHECK(result.has_value());
}

TEST_CASE("mailbox survives process restart via re-read from disk")
{
    TempDir temp{"codeharness-mailbox-restart-test"};

    {
        codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};
        REQUIRE(mailbox.send("recipient-001", make_test_message("s", "r", "first")).has_value());
        REQUIRE(mailbox.send("recipient-001", make_test_message("s", "r", "second")).has_value());
        REQUIRE(mailbox.send("recipient-001", make_test_message("s", "r", "third")).has_value());
    }

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

TEST_CASE("worker mailbox drain groups messages and marks them read")
{
    TempDir temp{"codeharness-worker-mailbox-drain-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    REQUIRE(mailbox.send("worker-001", make_test_message("leader", "worker-001", "first task")).has_value());

    auto shutdown = make_test_message("leader", "worker-001", "stop now");
    shutdown.type = codeharness::mailbox::MessageType::Shutdown;
    REQUIRE(mailbox.send("worker-001", std::move(shutdown)).has_value());

    auto permission = make_test_message("leader", "worker-001", "permission granted");
    permission.type = codeharness::mailbox::MessageType::PermissionResponse;
    REQUIRE(mailbox.send("worker-001", std::move(permission)).has_value());

    auto drained = codeharness::mailbox::drain_worker_mailbox(mailbox, "worker-001");
    REQUIRE(drained.has_value());
    CHECK(drained->shutdown_requested());
    REQUIRE(drained->user_messages.size() == 1);
    CHECK(drained->user_messages.front().content == "first task");
    REQUIRE(drained->shutdown_messages.size() == 1);
    CHECK(drained->shutdown_messages.front().content == "stop now");
    REQUIRE(drained->permission_responses.size() == 1);
    CHECK(drained->permission_responses.front().content == "permission granted");

    auto unread = mailbox.poll("worker-001", true);
    REQUIRE(unread.has_value());
    CHECK(unread->empty());

    auto all = mailbox.poll("worker-001", false);
    REQUIRE(all.has_value());
    REQUIRE(all->size() == 3);
    CHECK((*all)[0].read == true);
    CHECK((*all)[1].read == true);
    CHECK((*all)[2].read == true);
}

TEST_CASE("worker mailbox drain returns empty for missing inbox")
{
    TempDir temp{"codeharness-worker-mailbox-empty-drain-test"};
    codeharness::mailbox::Mailbox mailbox{temp.path / "mailboxes"};

    auto drained = codeharness::mailbox::drain_worker_mailbox(mailbox, "missing-worker");
    REQUIRE(drained.has_value());
    CHECK(!drained->shutdown_requested());
    CHECK(drained->user_messages.empty());
    CHECK(drained->task_results.empty());
    CHECK(drained->permission_requests.empty());
    CHECK(drained->permission_responses.empty());
    CHECK(drained->shutdown_messages.empty());
    CHECK(drained->idle_notifications.empty());
}

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
