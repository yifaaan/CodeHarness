#include "test_support.h"

#include "codeharness/gateway/bridge.h"
#include "codeharness/gateway/message_bus.h"
#include "codeharness/gateway/runtime_pool.h"
#include "codeharness/gateway/stdio_adapter.h"

using codeharness::gateway::GatewayBridge;
using codeharness::gateway::GatewayInboundMessage;
using codeharness::gateway::GatewayMessageBus;
using codeharness::gateway::GatewayMessageQueue;
using codeharness::gateway::GatewayOutboundMessage;
using codeharness::gateway::GatewayRuntime;
using codeharness::gateway::GatewayRuntimeFactory;
using codeharness::gateway::GatewayRuntimePool;
using codeharness::gateway::GatewaySessionKey;
using codeharness::gateway::GatewayStdioAdapter;
using codeharness::gateway::format_stdio_outbound_line;
using codeharness::gateway::normalize_session_key;
using codeharness::gateway::parse_stdio_inbound_line;
using codeharness::gateway::session_key_identity;

namespace
{

auto valid_key() -> GatewaySessionKey
{
    return GatewaySessionKey{
        .channel = "telegram",
        .conversation_id = "chat-1",
        .user_id = "user-1",
    };
}

auto valid_message(std::string text = "hello") -> GatewayInboundMessage
{
    return GatewayInboundMessage{
        .key = valid_key(),
        .text = std::move(text),
        .cwd = std::filesystem::current_path(),
    };
}

class RecordingRuntime final : public GatewayRuntime
{
public:
    RecordingRuntime(std::string label, std::vector<GatewayInboundMessage>* seen)
        : label_{std::move(label)}
        , seen_{seen}
    {
    }

    auto submit(GatewayInboundMessage message) -> codeharness::Result<GatewayOutboundMessage> override
    {
        seen_->push_back(message);
        return GatewayOutboundMessage{
            .key = message.key,
            .text = label_ + ':' + message.text,
        };
    }

private:
    std::string label_;
    std::vector<GatewayInboundMessage>* seen_ = nullptr;
};

class FailingRuntime final : public GatewayRuntime
{
public:
    auto submit(GatewayInboundMessage) -> codeharness::Result<GatewayOutboundMessage> override
    {
        return codeharness::fail<GatewayOutboundMessage>(
            codeharness::ErrorKind::Provider,
            "runtime unavailable");
    }
};

} // namespace

TEST_CASE("gateway session key trims fields and builds stable identity")
{
    auto key = GatewaySessionKey{
        .channel = " telegram ",
        .conversation_id = "\tchat:42\n",
        .user_id = " user|7 ",
    };

    auto normalized = normalize_session_key(std::move(key));
    CHECK(normalized.channel == "telegram");
    CHECK(normalized.conversation_id == "chat:42");
    CHECK(normalized.user_id == "user|7");
    CHECK(session_key_identity(normalized) == "8:telegram|7:chat:42|6:user|7");

    auto same_key = GatewaySessionKey{
        .channel = "telegram",
        .conversation_id = "chat:42",
        .user_id = "user|7",
    };
    CHECK(session_key_identity(same_key) == session_key_identity(normalized));
}

TEST_CASE("gateway runtime pool rejects invalid keys and blank input")
{
    auto factory_calls = 0;
    GatewayRuntimePool pool{[&factory_calls](const GatewaySessionKey&)
                                -> codeharness::Result<std::unique_ptr<GatewayRuntime>> {
        ++factory_calls;
        std::unique_ptr<GatewayRuntime> runtime = std::make_unique<FailingRuntime>();
        return std::move(runtime);
    }};

    auto missing_channel = valid_message();
    missing_channel.key.channel = " ";
    auto missing_channel_result = pool.submit(std::move(missing_channel));
    REQUIRE_FALSE(missing_channel_result.has_value());
    CHECK(missing_channel_result.error().kind == codeharness::ErrorKind::InvalidArgument);

    auto blank_text = valid_message(" \t\n ");
    auto blank_text_result = pool.submit(std::move(blank_text));
    REQUIRE_FALSE(blank_text_result.has_value());
    CHECK(blank_text_result.error().kind == codeharness::ErrorKind::InvalidArgument);
    CHECK(factory_calls == 0);
}

TEST_CASE("gateway runtime pool reuses runtime for the same session")
{
    std::vector<GatewayInboundMessage> seen;
    auto factory_calls = 0;

    GatewayRuntimePool pool{[&](const GatewaySessionKey& key)
                                -> codeharness::Result<std::unique_ptr<GatewayRuntime>> {
        ++factory_calls;
        CHECK(key == valid_key());

        std::unique_ptr<GatewayRuntime> runtime =
            std::make_unique<RecordingRuntime>("runtime-" + std::to_string(factory_calls), &seen);
        return std::move(runtime);
    }};

    auto first = pool.submit(valid_message(" first "));
    REQUIRE(first.has_value());
    CHECK(first->text == "runtime-1:first");

    auto second = pool.submit(valid_message("second"));
    REQUIRE(second.has_value());
    CHECK(second->text == "runtime-1:second");
    CHECK(second->key == valid_key());

    CHECK(factory_calls == 1);
    CHECK(pool.active_session_count() == 1);
    CHECK(pool.has_session(valid_key()));
    REQUIRE(seen.size() == 2);
    CHECK(seen.front().text == "first");
    CHECK(seen.back().text == "second");
}

TEST_CASE("gateway runtime pool separates different session keys")
{
    std::vector<GatewayInboundMessage> seen;
    auto factory_calls = 0;

    GatewayRuntimePool pool{[&](const GatewaySessionKey&)
                                -> codeharness::Result<std::unique_ptr<GatewayRuntime>> {
        ++factory_calls;
        std::unique_ptr<GatewayRuntime> runtime =
            std::make_unique<RecordingRuntime>("runtime-" + std::to_string(factory_calls), &seen);
        return std::move(runtime);
    }};

    auto first = valid_message("one");
    auto second = valid_message("two");
    second.key.conversation_id = "chat-2";
    auto third = valid_message("three");
    third.key.user_id = "user-2";

    REQUIRE(pool.submit(std::move(first)).has_value());
    REQUIRE(pool.submit(std::move(second)).has_value());
    REQUIRE(pool.submit(std::move(third)).has_value());

    CHECK(factory_calls == 3);
    CHECK(pool.active_session_count() == 3);
    CHECK(seen.size() == 3);
}

TEST_CASE("gateway runtime pool returns factory failures")
{
    GatewayRuntimePool pool{[](const GatewaySessionKey&)
                                -> codeharness::Result<std::unique_ptr<GatewayRuntime>> {
        return codeharness::fail<std::unique_ptr<GatewayRuntime>>(
            codeharness::ErrorKind::Config,
            "factory unavailable");
    }};

    auto result = pool.submit(valid_message());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::Config);
    CHECK(result.error().message == "factory unavailable");
    CHECK(pool.active_session_count() == 0);
}

TEST_CASE("gateway runtime pool returns runtime submit failures")
{
    GatewayRuntimePool pool{[](const GatewaySessionKey&)
                                -> codeharness::Result<std::unique_ptr<GatewayRuntime>> {
        std::unique_ptr<GatewayRuntime> runtime = std::make_unique<FailingRuntime>();
        return std::move(runtime);
    }};

    auto result = pool.submit(valid_message());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::Provider);
    CHECK(result.error().message == "runtime unavailable");
    CHECK(pool.active_session_count() == 1);
}

TEST_CASE("gateway message queue pops inbound messages in fifo order")
{
    GatewayMessageQueue<GatewayInboundMessage> queue;

    auto first = valid_message("one");
    auto second = valid_message("two");
    second.key.conversation_id = "chat-2";
    queue.push(std::move(first));
    queue.push(std::move(second));

    CHECK(queue.size() == 2);
    CHECK(!queue.empty());

    auto popped_first = queue.try_pop();
    REQUIRE(popped_first.has_value());
    CHECK(popped_first->text == "one");
    CHECK(popped_first->key.conversation_id == "chat-1");

    auto popped_second = queue.try_pop();
    REQUIRE(popped_second.has_value());
    CHECK(popped_second->text == "two");
    CHECK(popped_second->key.conversation_id == "chat-2");

    CHECK(queue.try_pop() == std::nullopt);
    CHECK(queue.empty());
}

TEST_CASE("gateway message queue drains outbound messages in fifo order")
{
    GatewayMessageQueue<GatewayOutboundMessage> queue;

    queue.push(GatewayOutboundMessage{
        .key = valid_key(),
        .text = "first",
    });
    queue.push(GatewayOutboundMessage{
        .key = GatewaySessionKey{
            .channel = "slack",
            .conversation_id = "thread-1",
            .user_id = "user-2",
        },
        .text = "second",
        .is_error = true,
    });

    auto drained = queue.drain();
    REQUIRE(drained.size() == 2);
    CHECK(drained.front().text == "first");
    CHECK(drained.back().text == "second");
    CHECK(drained.back().is_error);
    CHECK(queue.empty());

    auto drained_again = queue.drain();
    CHECK(drained_again.empty());
}

TEST_CASE("gateway message bus keeps inbound and outbound queues independent")
{
    GatewayMessageBus bus;

    bus.inbound().push(valid_message("incoming"));
    bus.outbound().push(GatewayOutboundMessage{
        .key = valid_key(),
        .text = "outgoing",
    });

    CHECK(bus.inbound().size() == 1);
    CHECK(bus.outbound().size() == 1);

    auto inbound = bus.inbound().try_pop();
    REQUIRE(inbound.has_value());
    CHECK(inbound->text == "incoming");
    CHECK(bus.inbound().empty());
    CHECK(bus.outbound().size() == 1);

    auto outbound = bus.outbound().try_pop();
    REQUIRE(outbound.has_value());
    CHECK(outbound->text == "outgoing");
    CHECK(bus.outbound().empty());
}

TEST_CASE("gateway bridge leaves queues unchanged when inbound is empty")
{
    GatewayMessageBus bus;
    GatewayRuntimePool pool{[](const GatewaySessionKey&)
                                -> codeharness::Result<std::unique_ptr<GatewayRuntime>> {
        FAIL("runtime factory should not be called");
        return codeharness::fail<std::unique_ptr<GatewayRuntime>>(
            codeharness::ErrorKind::Internal,
            "unexpected factory call");
    }};
    GatewayBridge bridge{bus, pool};

    auto result = bridge.process_next();
    REQUIRE(result.has_value());
    CHECK(!result->processed);
    CHECK(!result->published_error);
    CHECK(bus.outbound().empty());
}

TEST_CASE("gateway bridge publishes runtime response for one inbound message")
{
    GatewayMessageBus bus;
    std::vector<GatewayInboundMessage> seen;
    GatewayRuntimePool pool{[&seen](const GatewaySessionKey&)
                                -> codeharness::Result<std::unique_ptr<GatewayRuntime>> {
        std::unique_ptr<GatewayRuntime> runtime = std::make_unique<RecordingRuntime>("bridge", &seen);
        return std::move(runtime);
    }};
    GatewayBridge bridge{bus, pool};

    bus.inbound().push(valid_message("hello bridge"));

    auto result = bridge.process_next();
    REQUIRE(result.has_value());
    CHECK(result->processed);
    CHECK(!result->published_error);
    CHECK(bus.inbound().empty());
    CHECK(pool.active_session_count() == 1);
    REQUIRE(seen.size() == 1);
    CHECK(seen.front().text == "hello bridge");

    auto outbound = bus.outbound().try_pop();
    REQUIRE(outbound.has_value());
    CHECK(outbound->key == valid_key());
    CHECK(outbound->text == "bridge:hello bridge");
    CHECK(!outbound->is_error);
}

TEST_CASE("gateway bridge drains inbound messages in fifo order")
{
    GatewayMessageBus bus;
    std::vector<GatewayInboundMessage> seen;
    auto factory_calls = 0;
    GatewayRuntimePool pool{[&](const GatewaySessionKey&)
                                -> codeharness::Result<std::unique_ptr<GatewayRuntime>> {
        ++factory_calls;
        std::unique_ptr<GatewayRuntime> runtime =
            std::make_unique<RecordingRuntime>("runtime-" + std::to_string(factory_calls), &seen);
        return std::move(runtime);
    }};
    GatewayBridge bridge{bus, pool};

    bus.inbound().push(valid_message("first"));
    auto second = valid_message("second");
    second.key.conversation_id = "chat-2";
    bus.inbound().push(std::move(second));
    bus.inbound().push(valid_message("third"));

    auto processed = bridge.drain();
    REQUIRE(processed.has_value());
    CHECK(*processed == 3);
    CHECK(bus.inbound().empty());
    CHECK(factory_calls == 2);

    auto outbound = bus.outbound().drain();
    REQUIRE(outbound.size() == 3);
    CHECK(outbound[0].text == "runtime-1:first");
    CHECK(outbound[1].text == "runtime-2:second");
    CHECK(outbound[2].text == "runtime-1:third");
}

TEST_CASE("gateway bridge publishes runtime errors as outbound error messages")
{
    GatewayMessageBus bus;
    GatewayRuntimePool pool{[](const GatewaySessionKey&)
                                -> codeharness::Result<std::unique_ptr<GatewayRuntime>> {
        return codeharness::fail<std::unique_ptr<GatewayRuntime>>(
            codeharness::ErrorKind::Config,
            "factory unavailable");
    }};
    GatewayBridge bridge{bus, pool};

    auto message = valid_message("will fail");
    const auto key = message.key;
    bus.inbound().push(std::move(message));

    auto result = bridge.process_next();
    REQUIRE(result.has_value());
    CHECK(result->processed);
    CHECK(result->published_error);
    CHECK(pool.active_session_count() == 0);

    auto outbound = bus.outbound().try_pop();
    REQUIRE(outbound.has_value());
    CHECK(outbound->key == key);
    CHECK(outbound->text == "[gateway error] factory unavailable");
    CHECK(outbound->is_error);
}

TEST_CASE("gateway stdio adapter parses inbound json line")
{
    TempDir temp{"codeharness-gateway-stdio-parse-test"};
    const auto cwd = temp.path / "custom";
    const auto line = nlohmann::json{
        {"channel", " telegram "},
        {"conversation_id", " chat-1 "},
        {"user_id", " user-1 "},
        {"text", "hello from stdio"},
        {"cwd", cwd.string()},
    }.dump();

    auto parsed = parse_stdio_inbound_line(line, temp.path / "default");
    REQUIRE(parsed.has_value());
    CHECK(parsed->key == valid_key());
    CHECK(parsed->text == "hello from stdio");
    CHECK(parsed->cwd == cwd);
}

TEST_CASE("gateway stdio adapter uses default cwd when cwd is missing or empty")
{
    TempDir temp{"codeharness-gateway-stdio-default-cwd-test"};
    const auto default_cwd = temp.path / "default";

    auto missing_cwd = parse_stdio_inbound_line(
        nlohmann::json{
            {"channel", "telegram"},
            {"conversation_id", "chat-1"},
            {"user_id", "user-1"},
            {"text", "missing cwd"},
        }.dump(),
        default_cwd);
    REQUIRE(missing_cwd.has_value());
    CHECK(missing_cwd->cwd == default_cwd);

    auto empty_cwd = parse_stdio_inbound_line(
        nlohmann::json{
            {"channel", "telegram"},
            {"conversation_id", "chat-1"},
            {"user_id", "user-1"},
            {"text", "empty cwd"},
            {"cwd", " \t "},
        }.dump(),
        default_cwd);
    REQUIRE(empty_cwd.has_value());
    CHECK(empty_cwd->cwd == default_cwd);
}

TEST_CASE("gateway stdio adapter rejects malformed and invalid inbound lines")
{
    TempDir temp{"codeharness-gateway-stdio-invalid-test"};

    auto malformed = parse_stdio_inbound_line("{", temp.path);
    REQUIRE_FALSE(malformed.has_value());
    CHECK(malformed.error().kind == codeharness::ErrorKind::InvalidArgument);

    auto missing_text = parse_stdio_inbound_line(
        nlohmann::json{
            {"channel", "telegram"},
            {"conversation_id", "chat-1"},
            {"user_id", "user-1"},
        }.dump(),
        temp.path);
    REQUIRE_FALSE(missing_text.has_value());
    CHECK(missing_text.error().kind == codeharness::ErrorKind::InvalidArgument);

    auto wrong_type = parse_stdio_inbound_line(
        nlohmann::json{
            {"channel", 7},
            {"conversation_id", "chat-1"},
            {"user_id", "user-1"},
            {"text", "hello"},
        }.dump(),
        temp.path);
    REQUIRE_FALSE(wrong_type.has_value());
    CHECK(wrong_type.error().kind == codeharness::ErrorKind::InvalidArgument);
}

TEST_CASE("gateway stdio adapter formats outbound json line")
{
    const auto line = format_stdio_outbound_line(GatewayOutboundMessage{
        .key = valid_key(),
        .text = "done",
        .is_error = true,
    });

    REQUIRE(!line.empty());
    CHECK(line.back() == '\n');

    const auto json = nlohmann::json::parse(line);
    CHECK(json.at("channel") == "telegram");
    CHECK(json.at("conversation_id") == "chat-1");
    CHECK(json.at("user_id") == "user-1");
    CHECK(json.at("text") == "done");
    CHECK(json.at("is_error") == true);
}

TEST_CASE("gateway stdio adapter accepts inbound and drains outbound fifo")
{
    TempDir temp{"codeharness-gateway-stdio-adapter-test"};
    GatewayMessageBus bus;
    GatewayStdioAdapter adapter{bus, temp.path};

    auto first = nlohmann::json{
        {"channel", "telegram"},
        {"conversation_id", "chat-1"},
        {"user_id", "user-1"},
        {"text", "first"},
    };
    auto second = nlohmann::json{
        {"channel", "slack"},
        {"conversation_id", "thread-2"},
        {"user_id", "user-2"},
        {"text", "second"},
    };

    REQUIRE(adapter.accept_line(first.dump()).has_value());
    REQUIRE(adapter.accept_line(second.dump()).has_value());

    auto inbound = bus.inbound().drain();
    REQUIRE(inbound.size() == 2);
    CHECK(inbound[0].text == "first");
    CHECK(inbound[0].key.channel == "telegram");
    CHECK(inbound[1].text == "second");
    CHECK(inbound[1].key.channel == "slack");

    bus.outbound().push(GatewayOutboundMessage{
        .key = inbound[0].key,
        .text = "first reply",
    });
    bus.outbound().push(GatewayOutboundMessage{
        .key = inbound[1].key,
        .text = "second reply",
        .is_error = true,
    });

    auto lines = adapter.drain_outbound_lines();
    REQUIRE(lines.size() == 2);
    CHECK(nlohmann::json::parse(lines[0]).at("text") == "first reply");
    CHECK(nlohmann::json::parse(lines[0]).at("is_error") == false);
    CHECK(nlohmann::json::parse(lines[1]).at("text") == "second reply");
    CHECK(nlohmann::json::parse(lines[1]).at("is_error") == true);
    CHECK(bus.outbound().empty());
}
