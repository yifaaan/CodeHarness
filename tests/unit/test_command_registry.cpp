#include <doctest/doctest.h>

#include <deque>
#include <filesystem>
#include <string>

#include "codeharness/api/mock_client.h"
#include "codeharness/commands/command_registry.h"
#include "codeharness/engine/query_engine.h"
#include "codeharness/permissions/checker.h"
#include "codeharness/tools/tool_registry.h"

using namespace codeharness;

TEST_CASE("command registry ignores normal user text") {
    const auto registry = commands::CommandRegistry{};

    const auto result = registry.try_dispatch(commands::CommandContext{}, "hello model");

    CHECK_FALSE(result.handled);
    CHECK_FALSE(result.should_exit);
    CHECK(result.message.empty());
}

TEST_CASE("command registry returns help text") {
    const auto registry = commands::CommandRegistry{};

    const auto result = registry.try_dispatch(commands::CommandContext{}, "/help");

    REQUIRE(result.handled);
    CHECK_FALSE(result.should_exit);
    CHECK(result.message.find("Available commands:") != std::string::npos);
    CHECK(result.message.find("/help") != std::string::npos);
    CHECK(result.message.find("/clear") != std::string::npos);
    CHECK(result.message.find("/status") != std::string::npos);
    CHECK(result.message.find("/exit") != std::string::npos);
}

TEST_CASE("command registry reports unknown slash command as handled") {
    const auto registry = commands::CommandRegistry{};

    const auto result = registry.try_dispatch(commands::CommandContext{}, "/missing arg");

    REQUIRE(result.handled);
    CHECK_FALSE(result.should_exit);
    CHECK(result.message.find("Unknown command: /missing") != std::string::npos);
    CHECK(result.message.find("/help") != std::string::npos);
}

TEST_CASE("command registry handles exit command") {
    const auto registry = commands::CommandRegistry{};

    const auto result = registry.try_dispatch(commands::CommandContext{}, "/exit");

    REQUIRE(result.handled);
    CHECK(result.should_exit);
    CHECK(result.message.find("Goodbye") != std::string::npos);
}

TEST_CASE("command registry status reports engine and tool state") {
    auto client = api::MockClient{std::deque<api::MockClient::Response>{}};
    auto tool_registry = tools::ToolRegistry{};
    auto permission_checker = permissions::PermissionChecker{permissions::PermissionSettings{}};

    auto engine = engine::QueryEngine{
        client,       tool_registry,   permission_checker, std::filesystem::current_path(),
        "mock-model", "system-prompt",
    };

    const auto registry = commands::CommandRegistry{};
    const auto result = registry.try_dispatch(
        commands::CommandContext{
            .engine = &engine,
            .tools = &tool_registry,
        },
        "/status");

    REQUIRE(result.handled);
    CHECK(result.message.find("Messages: 0") != std::string::npos);
    CHECK(result.message.find("Usage: input=0 output=0") != std::string::npos);
    CHECK(result.message.find("Tools: 0") != std::string::npos);
}