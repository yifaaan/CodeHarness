#include "test_support.h"

#include <stdexcept>

namespace
{

class CapturingProvider final : public codeharness::Provider
{
public:
    std::vector<codeharness::Message> seen_messages;

    auto stream(std::span<const codeharness::Message> messages, const codeharness::ProviderEventSink& sink) -> codeharness::Result<void> override
    {
        seen_messages.assign(messages.begin(), messages.end());
        sink(codeharness::AssistantTextDelta{"ok"});
        sink(codeharness::MessageFinished{});
        return {};
    }
};

class CountingProvider final : public codeharness::Provider
{
public:
    int calls = 0;

    auto stream(std::span<const codeharness::Message>, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        ++calls;
        sink(codeharness::AssistantTextDelta{"should not run"});
        sink(codeharness::MessageFinished{});
        return {};
    }
};

class ToolThenTextProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message>, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        sink(codeharness::AssistantTextDelta{"partial"});
        sink(
            codeharness::ToolUseStarted{
                .id = "tool-use-1",
                .name = "read_file",
            });
        sink(
            codeharness::ToolUseInputDelta{
                .id = "tool-use-1",
                .input_json_delta = R"({"path":"hello.txt"})",
            });
        sink(codeharness::ToolUseFinished{.id = "tool-use-1"});
        sink(codeharness::MessageFinished{});
        return {};
    }
};

class UsageProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message>, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        sink(codeharness::ProviderUsage{.input_tokens = 3, .output_tokens = 2, .total_tokens = 5});
        sink(codeharness::AssistantTextDelta{"ok"});
        sink(codeharness::MessageFinished{});
        return {};
    }
};

class MultiTurnUsageProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message> messages, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        ++calls;
        if (calls == 1)
        {
            sink(codeharness::ProviderUsage{.input_tokens = 4, .output_tokens = 3});
            sink(codeharness::ToolUseStarted{.id = "tool-use-1", .name = "read_file"});
            sink(codeharness::ToolUseInputDelta{.id = "tool-use-1", .input_json_delta = R"({"path":"hello.txt"})"});
            sink(codeharness::ToolUseFinished{.id = "tool-use-1"});
            sink(codeharness::MessageFinished{});
            return {};
        }

        sink(codeharness::ProviderUsage{.input_tokens = 8, .output_tokens = 6});
        sink(codeharness::AssistantTextDelta{"done"});
        sink(codeharness::MessageFinished{});
        return {};
    }

    int calls = 0;
};

class AskUserProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message> messages, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        if (!messages.empty() && messages.back().role == codeharness::Role::Tool)
        {
            for (const auto& block : messages.back().content)
            {
                if (const auto* result = std::get_if<codeharness::ToolResultBlock>(&block))
                {
                    sink(codeharness::AssistantTextDelta{result->content});
                    sink(codeharness::MessageFinished{});
                    return {};
                }
            }
            sink(codeharness::MessageFinished{});
            return {};
        }

        sink(codeharness::ToolUseStarted{.id = "tool-use-ask", .name = "ask_user"});
        sink(codeharness::ToolUseInputDelta{
            .id = "tool-use-ask",
            .input_json_delta = R"({"question":"Which file?","reason":"Need a target"})",
        });
        sink(codeharness::ToolUseFinished{.id = "tool-use-ask"});
        sink(codeharness::MessageFinished{});
        return {};
    }
};

} // namespace

TEST_CASE("engine runs one provider turn")
{
    codeharness::EchoProvider provider;
    codeharness::ToolRegistry tools;
    codeharness::Engine engine{provider, tools};

    codeharness::RunRequest request;
    request.prompt = "hello";
    request.options.max_turns = 1;

    auto result = engine.run(request);

    REQUIRE(result.has_value());
    CHECK(result->output_text == "hello");
    REQUIRE(result->messages.size() == 2);
    CHECK(result->messages[0].role == codeharness::Role::User);
    CHECK(result->messages[1].role == codeharness::Role::Assistant);
}

TEST_CASE("engine ask_user calls user question handler and returns answer")
{
    AskUserProvider provider;
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::AskUserTool>());
    codeharness::Engine engine{provider, tools};

    codeharness::RunRequest request;
    request.prompt = "ask";
    request.options.max_turns = 2;
    request.user_question = [](const codeharness::UserQuestionPrompt& prompt) -> codeharness::Result<codeharness::UserQuestionResponse> {
        CHECK(prompt.id == "ask-tool-use-ask");
        CHECK(prompt.tool_use_id == "tool-use-ask");
        CHECK(prompt.question == "Which file?");
        CHECK(prompt.reason == "Need a target");
        return codeharness::UserQuestionResponse{.answer = "src/main.cpp"};
    };

    auto result = engine.run(request);

    REQUIRE(result.has_value());
    CHECK(result->output_text.find("src/main.cpp") != std::string::npos);
}

TEST_CASE("engine ask_user without handler returns model visible tool error")
{
    AskUserProvider provider;
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::AskUserTool>());
    codeharness::Engine engine{provider, tools};

    codeharness::RunRequest request;
    request.prompt = "ask";
    request.options.max_turns = 2;

    auto result = engine.run(request);

    REQUIRE(result.has_value());
    CHECK(result->output_text.find("user input unavailable in non-interactive mode") != std::string::npos);
}

TEST_CASE("engine ask_user accepts empty answer")
{
    AskUserProvider provider;
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::AskUserTool>());
    codeharness::Engine engine{provider, tools};

    codeharness::RunRequest request;
    request.prompt = "ask";
    request.options.max_turns = 2;
    request.user_question = [](const codeharness::UserQuestionPrompt&) -> codeharness::Result<codeharness::UserQuestionResponse> {
        return codeharness::UserQuestionResponse{.answer = ""};
    };

    auto result = engine.run(request);

    REQUIRE(result.has_value());
    CHECK(result->messages.back().role == codeharness::Role::Assistant);
}

TEST_CASE("engine prepends system prompt when provided")
{
    CapturingProvider provider;
    codeharness::ToolRegistry tools;
    codeharness::Engine engine{provider, tools};

    codeharness::RunRequest request;
    request.prompt = "hello";
    request.system_prompt = "system rules";
    request.options.max_turns = 1;

    auto result = engine.run(request);

    REQUIRE(result.has_value());
    CHECK(result->output_text == "ok");
    REQUIRE(result->messages.size() == 3);
    CHECK(result->messages[0].role == codeharness::Role::System);
    CHECK(codeharness::collect_text(result->messages[0]) == "system rules");
    CHECK(result->messages[1].role == codeharness::Role::User);
    CHECK(result->messages[2].role == codeharness::Role::Assistant);

    REQUIRE(provider.seen_messages.size() == 2);
    CHECK(provider.seen_messages[0].role == codeharness::Role::System);
    CHECK(provider.seen_messages[1].role == codeharness::Role::User);
}

TEST_CASE("engine continues from initial messages and replaces prior system prompt")
{
    CapturingProvider provider;
    codeharness::ToolRegistry tools;
    codeharness::Engine engine{provider, tools};

    codeharness::RunRequest request;
    request.prompt = "next";
    request.system_prompt = "fresh system";
    request.initial_messages = std::vector<codeharness::Message>{
        codeharness::make_text_message(codeharness::Role::System, "old system"),
        codeharness::make_text_message(codeharness::Role::User, "previous"),
        codeharness::make_text_message(codeharness::Role::Assistant, "prior answer"),
    };
    request.options.max_turns = 1;

    auto result = engine.run(request);

    REQUIRE(result.has_value());
    REQUIRE(result->messages.size() == 5);
    CHECK(result->messages[0].role == codeharness::Role::System);
    CHECK(codeharness::collect_text(result->messages[0]) == "fresh system");
    CHECK(result->messages[1].role == codeharness::Role::User);
    CHECK(codeharness::collect_text(result->messages[1]) == "previous");
    CHECK(result->messages[2].role == codeharness::Role::Assistant);
    CHECK(result->messages[3].role == codeharness::Role::User);
    CHECK(codeharness::collect_text(result->messages[3]) == "next");
    CHECK(result->messages[4].role == codeharness::Role::Assistant);

    REQUIRE(provider.seen_messages.size() == 4);
    CHECK(codeharness::collect_text(provider.seen_messages[0]) == "fresh system");
    CHECK(codeharness::collect_text(provider.seen_messages[3]) == "next");
}

namespace
{

class ReadFileRequestProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message> messages, const codeharness::ProviderEventSink& sink) -> codeharness::Result<void> override
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
                .name = "read_file",
            });
        sink(
            codeharness::ToolUseInputDelta{
                .id = "tool-use-1",
                .input_json_delta = R"({"path":"hello.txt"})",
            });
        sink(codeharness::ToolUseFinished{.id = "tool-use-1"});
        sink(codeharness::MessageFinished{});

        return {};
    }
};

class ThrowingToolRequestProvider final : public codeharness::Provider
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
                .id = "tool-use-throw",
                .name = "throw_tool",
            });
        sink(
            codeharness::ToolUseInputDelta{
                .id = "tool-use-throw",
                .input_json_delta = R"({})",
            });
        sink(codeharness::ToolUseFinished{.id = "tool-use-throw"});
        sink(codeharness::MessageFinished{});

        return {};
    }
};

class ThrowingTool final : public codeharness::Tool
{
public:
    auto name() const -> std::string override
    {
        return "throw_tool";
    }

    auto description() const -> std::string override
    {
        return "Throws from execute.";
    }

    auto execute(const codeharness::ToolRequest&, const codeharness::ToolContext&) const
        -> codeharness::Result<codeharness::ToolResponse> override
    {
        throw std::runtime_error{"boom"};
    }
};

} // namespace

TEST_CASE("engine executes requested tool and returns final provider text")
{
    auto temp_dir = std::filesystem::temp_directory_path() / "codeharness-engine-tool-test";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);

    {
        std::ofstream file{temp_dir / "hello.txt"};
        file << "hello from engine file";
    }

    auto previous_cwd = std::filesystem::current_path();
    std::filesystem::current_path(temp_dir);

    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::ReadFileTool>());

    ReadFileRequestProvider provider;
    codeharness::Engine engine{provider, tools};

    codeharness::RunRequest request;
    request.prompt = "read hello.txt";
    request.options.max_turns = 3;

    auto result = engine.run(request);

    std::filesystem::current_path(previous_cwd);
    std::filesystem::remove_all(temp_dir);

    /*
        -> User prompt
        -> Assistant 请求调用工具
        -> Tool 返回工具结果
        -> Assistant 根据工具结果给最终回复
    */
    REQUIRE(result.has_value());
    CHECK(result->output_text == "hello from engine file");
    REQUIRE(result->messages.size() == 4);
    CHECK(result->messages[0].role == codeharness::Role::User);      // messages[0] User"read hello.txt"
    CHECK(result->messages[1].role == codeharness::Role::Assistant); // messages[1] Assistant ToolUseBlock: read_file({"path":"hello.txt"})
    CHECK(result->messages[2].role == codeharness::Role::Tool);      // messages[2] Tool ToolResultBlock: "hello from engine file"
    CHECK(result->messages[3].role == codeharness::Role::Assistant); // messages[3] Assistant TextBlock: "hello from engine file"
}

TEST_CASE("engine reports unknown tool as a tool error")
{
    ReadFileRequestProvider provider;
    codeharness::ToolRegistry tools;
    codeharness::Engine engine{provider, tools};

    codeharness::RunRequest request;
    request.prompt = "read hello.txt";
    request.options.max_turns = 3;

    auto result = engine.run(request);

    REQUIRE(result.has_value());
    REQUIRE(result->messages.size() == 4);
    CHECK(result->messages[2].role == codeharness::Role::Tool);

    auto tool_result = std::get_if<codeharness::ToolResultBlock>(&result->messages[2].content.front());
    REQUIRE(tool_result != nullptr);
    CHECK(tool_result->tool_use_id == "tool-use-1");
    CHECK(tool_result->is_error);
    CHECK(tool_result->content == "tool not found: read_file");
    CHECK(result->output_text == tool_result->content);
}

TEST_CASE("engine reports throwing tool as a tool error")
{
    ThrowingToolRequestProvider provider;
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<ThrowingTool>());
    codeharness::Engine engine{provider, tools};

    codeharness::RunRequest request;
    request.prompt = "run throwing tool";
    request.options.max_turns = 3;

    auto result = engine.run(request);

    REQUIRE(result.has_value());
    REQUIRE(result->messages.size() == 4);
    CHECK(result->messages[2].role == codeharness::Role::Tool);

    auto tool_result = std::get_if<codeharness::ToolResultBlock>(&result->messages[2].content.front());
    REQUIRE(tool_result != nullptr);
    CHECK(tool_result->tool_use_id == "tool-use-throw");
    CHECK(tool_result->is_error);
    CHECK(tool_result->content.find("unexpected exception: boom") != std::string::npos);
    CHECK(result->output_text == tool_result->content);
}

TEST_CASE("engine streaming emits assistant text delta")
{
    codeharness::EchoProvider provider;
    codeharness::ToolRegistry tools;
    codeharness::Engine engine{provider, tools};

    codeharness::RunRequest request;
    request.prompt = "hello";
    request.options.max_turns = 1;

    std::string streamed_text;

    auto result = engine.run_streaming(request, [&](const codeharness::EngineEvent& event) {
        if (const auto* delta = std::get_if<codeharness::EngineAssistantTextDelta>(&event))
        {
            streamed_text += delta->text;
        }
    });

    REQUIRE(result.has_value());
    CHECK(streamed_text == "hello");
    CHECK(result->output_text == "hello");
}

TEST_CASE("engine streaming emits tool input deltas")
{
    ToolThenTextProvider provider;
    codeharness::ToolRegistry tools;
    codeharness::Engine engine{provider, tools};

    codeharness::RunRequest request;
    request.prompt = "read";
    request.options.max_turns = 1;

    std::string streamed_input;
    auto result = engine.run_streaming(request, [&](const codeharness::EngineEvent& event) {
        if (const auto* delta = std::get_if<codeharness::EngineToolInputDelta>(&event))
        {
            CHECK(delta->id == "tool-use-1");
            streamed_input += delta->input_json_delta;
        }
    });

    REQUIRE(!result.has_value());
    CHECK(result.error().message == "max turns exceeded");
    CHECK(streamed_input == R"({"path":"hello.txt"})");
}

TEST_CASE("engine aggregates usage events into run result")
{
    UsageProvider provider;
    codeharness::ToolRegistry tools;
    codeharness::Engine engine{provider, tools};

    codeharness::RunRequest request;
    request.prompt = "hello";
    request.options.max_turns = 1;

    auto result = engine.run(request);

    REQUIRE(result.has_value());
    CHECK(result->usage.input_tokens == 3);
    CHECK(result->usage.output_tokens == 2);
    CHECK(result->usage.total_tokens == 5);
}

TEST_CASE("engine sums usage across provider turns")
{
    auto temp_dir = std::filesystem::temp_directory_path() / "codeharness-engine-usage-test";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);

    {
        std::ofstream file{temp_dir / "hello.txt"};
        file << "hello";
    }

    auto previous_cwd = std::filesystem::current_path();
    std::filesystem::current_path(temp_dir);

    MultiTurnUsageProvider provider;
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::ReadFileTool>());
    codeharness::Engine engine{provider, tools};

    codeharness::RunRequest request;
    request.prompt = "read";
    request.options.max_turns = 3;

    auto result = engine.run(request);

    std::filesystem::current_path(previous_cwd);
    std::filesystem::remove_all(temp_dir);

    REQUIRE(result.has_value());
    CHECK(result->usage.input_tokens == 12);
    CHECK(result->usage.output_tokens == 9);
    CHECK(result->usage.total_tokens == 21);
}

TEST_CASE("engine cancellation before provider turn skips provider")
{
    CountingProvider provider;
    codeharness::ToolRegistry tools;
    codeharness::Engine engine{provider, tools};
    codeharness::CancellationSource cancellation;
    cancellation.cancel();

    codeharness::RunRequest request;
    request.prompt = "hello";
    request.cancellation = cancellation.token();
    request.options.max_turns = 1;

    auto result = engine.run(request);

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::Cancelled);
    CHECK(result.error().message == "interrupted");
    CHECK(provider.calls == 0);
}

TEST_CASE("engine cancellation after provider delta stops before tool execution")
{
    ToolThenTextProvider provider;
    codeharness::ToolRegistry tools;
    tools.add(std::make_unique<codeharness::ReadFileTool>());
    codeharness::Engine engine{provider, tools};
    codeharness::CancellationSource cancellation;

    codeharness::RunRequest request;
    request.prompt = "read";
    request.cancellation = cancellation.token();
    request.options.max_turns = 2;

    bool saw_delta = false;
    auto result = engine.run_streaming(request, [&](const codeharness::EngineEvent& event) {
        if (std::holds_alternative<codeharness::EngineAssistantTextDelta>(event))
        {
            saw_delta = true;
            cancellation.cancel();
        }
    });

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::Cancelled);
    CHECK(saw_delta);
}
