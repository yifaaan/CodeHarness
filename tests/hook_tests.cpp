#include "test_support.h"

TEST_CASE("hook registry returns priority order and preserves registration order")
{
    codeharness::HookRegistry registry;
    std::vector<std::string> calls;

    registry.add(
        codeharness::HookDefinition{
            .event = codeharness::HookEvent::PreToolUse,
            .priority = 10,
            .callback =
                [&](const nlohmann::json&) {
                    calls.push_back("first-10");
                    return codeharness::HookResult{};
                },
        });
    registry.add(
        codeharness::HookDefinition{
            .event = codeharness::HookEvent::PreToolUse,
            .priority = 20,
            .callback =
                [&](const nlohmann::json&) {
                    calls.push_back("priority-20");
                    return codeharness::HookResult{};
                },
        });
    registry.add(
        codeharness::HookDefinition{
            .event = codeharness::HookEvent::PreToolUse,
            .priority = 10,
            .callback =
                [&](const nlohmann::json&) {
                    calls.push_back("second-10");
                    return codeharness::HookResult{};
                },
        });

    codeharness::HookExecutor executor{registry};
    auto result = executor.execute(codeharness::HookEvent::PreToolUse, nlohmann::json{{"tool_name", "bash"}});

    CHECK(!result.blocked);
    CHECK(calls == std::vector<std::string>{"priority-20", "first-10", "second-10"});
}

TEST_CASE("hook executor skips unmatched tool matcher")
{
    codeharness::HookRegistry registry;
    bool called = false;

    registry.add(
        codeharness::HookDefinition{
            .event = codeharness::HookEvent::PreToolUse,
            .matcher = std::string{"bash"},
            .callback =
                [&](const nlohmann::json&) {
                    called = true;
                    return codeharness::HookResult{};
                },
        });

    codeharness::HookExecutor executor{registry};
    auto result = executor.execute(codeharness::HookEvent::PreToolUse, nlohmann::json{{"tool_name", "write_file"}});

    CHECK(!result.blocked);
    CHECK(!called);
}

TEST_CASE("command hook receives payload in environment")
{
#if defined(_WIN32)
    const std::string command = "echo %CODEHARNESS_HOOK_PAYLOAD%";
#else
    const std::string command = "printf '%s\\n' \"$CODEHARNESS_HOOK_PAYLOAD\"";
#endif

    codeharness::HookRegistry registry;
    registry.add(
        codeharness::HookDefinition{
            .event = codeharness::HookEvent::PreToolUse,
            .type = codeharness::HookType::Command,
            .matcher = std::string{"write_file"},
            .config = nlohmann::json{{"command", command}},
        });

    codeharness::HookExecutor executor{registry};
    auto result = executor.execute(
        codeharness::HookEvent::PreToolUse,
        nlohmann::json{
            {"tool_use_id", "tool-use-1"},
            {"tool_name", "write_file"},
        });

    REQUIRE(result.results.size() == 1);
    CHECK(!result.blocked);
    CHECK(result.results.front().success);
    CHECK(result.results.front().output.find("write_file") != std::string::npos);
}

TEST_CASE("blocking command hook failure blocks execution")
{
#if defined(_WIN32)
    const std::string command = "exit /b 7";
#else
    const std::string command = "exit 7";
#endif

    codeharness::HookRegistry registry;
    registry.add(
        codeharness::HookDefinition{
            .event = codeharness::HookEvent::PreToolUse,
            .type = codeharness::HookType::Command,
            .block_on_failure = true,
            .config = nlohmann::json{{"command", command}},
        });

    codeharness::HookExecutor executor{registry};
    auto result = executor.execute(codeharness::HookEvent::PreToolUse, nlohmann::json{{"tool_name", "bash"}});

    REQUIRE(result.results.size() == 1);
    CHECK(result.blocked);
    CHECK(!result.results.front().success);
    CHECK(!result.reason.empty());
}

TEST_CASE("engine lets pre-tool hook block tool execution")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;

    codeharness::PermissionChecker checker{settings};
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::WriteFileTool>());

    codeharness::HookRegistry registry;
    registry.add(
        codeharness::HookDefinition{
            .event = codeharness::HookEvent::PreToolUse,
            .matcher = std::string{"write_file"},
            .callback = [](const nlohmann::json&) { return codeharness::HookResult{.success = true, .blocked = true, .reason = "writes disabled"}; },
        });
    codeharness::HookExecutor hooks{registry};

    WriteFileRequestProvider provider;
    codeharness::Engine engine{provider, tools, &checker, &hooks};

    codeharness::RunRequest request;
    request.prompt = "write output.txt";
    request.options.max_turns = 3;

    auto result = engine.run(request);

    REQUIRE(result.has_value());
    CHECK(result->output_text.find("hook blocked tool execution: writes disabled") != std::string::npos);
}

TEST_CASE("engine post-tool hook receives tool result payload")
{
    TempDir temp{"codeharness-engine-post-hook-test"};

    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;

    codeharness::PermissionChecker checker{settings};
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::WriteFileTool>());

    std::string observed_content;
    codeharness::HookRegistry registry;
    registry.add(
        codeharness::HookDefinition{
            .event = codeharness::HookEvent::PostToolUse,
            .matcher = std::string{"write_file"},
            .callback =
                [&](const nlohmann::json& payload) {
                    observed_content = payload.at("result").at("content").get<std::string>();
                    return codeharness::HookResult{};
                },
        });
    codeharness::HookExecutor hooks{registry};

    WriteFileRequestProvider provider;
    codeharness::Engine engine{provider, tools, &checker, &hooks};

    auto previous_cwd = std::filesystem::current_path();
    std::filesystem::current_path(temp.path);

    codeharness::RunRequest request;
    request.prompt = "write output.txt";
    request.options.max_turns = 3;

    auto result = engine.run(request);

    std::filesystem::current_path(previous_cwd);

    REQUIRE(result.has_value());
    CHECK(observed_content.find("Created") != std::string::npos);
    CHECK(result->output_text.find("Created") != std::string::npos);
}
