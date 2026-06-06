#include "test_support.h"

namespace
{

class DangerousBashRequestProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message> messages, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        for (auto& message : messages)
        {
            if (message.role != codeharness::Role::Tool)
            {
                continue;
            }

            for (auto& block : message.content)
            {
                if (auto result = std::get_if<codeharness::ToolResultBlock>(&block))
                {
                    sink(codeharness::AssistantTextDelta{result->content});
                    sink(codeharness::MessageFinished{});
                    return {};
                }
            }
        }

        sink(
            codeharness::ToolUseStarted{
                .id = "tool-use-1",
                .name = "bash",
            });
        sink(
            codeharness::ToolUseInputDelta{
                .id = "tool-use-1",
                .input_json_delta = R"({"command":"printf 'rm -rf /'"})",
            });
        sink(codeharness::ToolUseFinished{.id = "tool-use-1"});
        sink(codeharness::MessageFinished{});

        return {};
    }
};

} // namespace

TEST_CASE("engine blocks write_file in default mode without permission prompt")
{
    // Default 模式下 write_file 会返回 Ask，
    // 但 Engine 没有配置 UI 确认回调，当成拒绝。
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Default;

    codeharness::PermissionChecker checker{settings};
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::WriteFileTool>());

    WriteFileRequestProvider provider;
    codeharness::Engine engine{provider, tools, &checker};

    codeharness::RunRequest request;
    request.prompt = "write hello.txt";
    request.options.max_turns = 3;

    auto result = engine.run(request);

    // engine 不会崩溃，但最终输出应包含权限确认信息（Ask → 无 UI → 当做拒绝）
    REQUIRE(result.has_value());
    CHECK(result->output_text.find("permission confirmation required") != std::string::npos);
}

TEST_CASE("engine executes default-mode mutating tool when permission prompt approves")
{
    TempDir temp{"codeharness-engine-permission-approve-test"};

    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Default;

    codeharness::PermissionChecker checker{settings};
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::WriteFileTool>());

    WriteFileRequestProvider provider;
    codeharness::Engine engine{provider, tools, &checker};

    auto previous_cwd = std::filesystem::current_path();
    std::filesystem::current_path(temp.path);

    int prompt_count = 0;
    codeharness::RunRequest request;
    request.prompt = "write output.txt";
    request.permission_prompt = [&](const codeharness::PermissionPrompt& prompt) -> codeharness::Result<codeharness::PermissionResponse> {
        prompt_count++;
        CHECK(prompt.tool_name == "write_file");
        CHECK(prompt.path.has_value());
        return codeharness::PermissionResponse{.allowed = true};
    };
    request.options.max_turns = 3;

    auto result = engine.run(request);
    std::filesystem::current_path(previous_cwd);

    REQUIRE(result.has_value());
    CHECK(prompt_count == 1);
    CHECK(result->output_text.find("Created") != std::string::npos);
    CHECK(std::filesystem::exists(temp.path / "output.txt"));
}

TEST_CASE("engine returns tool error when permission prompt denies")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Default;

    codeharness::PermissionChecker checker{settings};
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::WriteFileTool>());

    WriteFileRequestProvider provider;
    codeharness::Engine engine{provider, tools, &checker};

    codeharness::RunRequest request;
    request.prompt = "write output.txt";
    request.permission_prompt = [](const codeharness::PermissionPrompt&) -> codeharness::Result<codeharness::PermissionResponse> {
        return codeharness::PermissionResponse{.allowed = false, .reason = "test denied"};
    };
    request.options.max_turns = 3;

    auto result = engine.run(request);

    REQUIRE(result.has_value());
    CHECK(result->output_text.find("permission denied: test denied") != std::string::npos);
}

TEST_CASE("engine allows write_file in full_auto mode")
{
    TempDir temp{"codeharness-engine-fullauto-test"};

    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;

    codeharness::PermissionChecker checker{settings};
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::WriteFileTool>());

    WriteFileRequestProvider provider;
    codeharness::Engine engine{provider, tools, &checker};

    auto previous_cwd = std::filesystem::current_path();
    std::filesystem::current_path(temp.path);

    codeharness::RunRequest request;
    request.prompt = "write output.txt";
    request.options.max_turns = 3;

    auto result = engine.run(request);

    std::filesystem::current_path(previous_cwd);

    REQUIRE(result.has_value());
    // full_auto 模式下写操作应该成功执行
    CHECK(result->output_text.find("Created") != std::string::npos);
}

TEST_CASE("engine blocks write_file in plan mode")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Plan;

    codeharness::PermissionChecker checker{settings};
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::WriteFileTool>());

    WriteFileRequestProvider provider;
    codeharness::Engine engine{provider, tools, &checker};

    codeharness::RunRequest request;
    request.prompt = "write hello.txt";
    request.options.max_turns = 3;

    auto result = engine.run(request);

    REQUIRE(result.has_value());
    CHECK(result->output_text.find("permission denied") != std::string::npos);
}

TEST_CASE("engine passes bash command to permission checker")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;

    codeharness::PermissionChecker checker{settings};
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::BashTool>());

    DangerousBashRequestProvider provider;
    codeharness::Engine engine{provider, tools, &checker};

    codeharness::RunRequest request;
    request.prompt = "run a command";
    request.options.max_turns = 3;

    auto result = engine.run(request);

    REQUIRE(result.has_value());
    CHECK(result->output_text.find("permission denied: dangerous command is blocked") != std::string::npos);
}
