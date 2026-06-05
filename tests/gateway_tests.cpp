#include "test_support.h"

#include "codeharness/gateway/message_bus.h"
#include "codeharness/gateway/runtime_pool.h"

using codeharness::gateway::GatewayInboundMessage;
using codeharness::gateway::GatewayMessageBus;
using codeharness::gateway::GatewayMessageQueue;
using codeharness::gateway::GatewayOutboundMessage;
using codeharness::gateway::GatewayRuntime;
using codeharness::gateway::GatewayRuntimeFactory;
using codeharness::gateway::GatewayRuntimePool;
using codeharness::gateway::GatewaySessionKey;
using codeharness::gateway::normalize_session_key;
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
