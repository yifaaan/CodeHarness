#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "codeharness/core/message.h"
#include "codeharness/engine/engine.h"
#include "codeharness/permissions/permission.h"
#include "codeharness/provider/echo_provider.h"
#include "codeharness/provider/provider.h"
#include "codeharness/tools/read_file_tool.h"
#include "codeharness/tools/tool_registry.h"
#include "codeharness/tools/write_file_tool.h"
#include "codeharness/version.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <vector>

namespace
{

struct TempDir
{
    std::filesystem::path path;

    explicit TempDir(std::string name) : path(std::filesystem::temp_directory_path() / std::move(name))
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
        std::filesystem::create_directories(path);
    }

    ~TempDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

auto read_file_text(const std::filesystem::path& path) -> std::string
{
    std::ifstream file{path, std::ios::binary};

    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("project metadata is available")
{
    CHECK(codeharness::PROJECT_NAME == "CodeHarness");
    CHECK(codeharness::VERSION == "0.1.0");
}

TEST_CASE("echo provider returns latest user text")
{
    codeharness::EchoProvider provider;

    std::vector<codeharness::Message> messages;
    messages.push_back(codeharness::make_text_message(codeharness::Role::User, "hello"));

    auto result = provider.generate(std::span<const codeharness::Message>(messages));

    REQUIRE(result.has_value());
    CHECK(result->role == codeharness::Role::Assistant);
    CHECK(codeharness::collect_text(*result) == "hello");
}

TEST_CASE("engine runs one provider turn")
{
    codeharness::EchoProvider provider;
    codeharness::Engine engine{provider};

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

TEST_CASE("tool registry executes read_file")
{
    const auto temp_dir = std::filesystem::temp_directory_path() / "codeharness-read-file-test";
    std::filesystem::create_directories(temp_dir);

    auto file_path = temp_dir / "hello.txt";
    {
        std::ofstream file{file_path};
        file << "hello from file";
    }

    codeharness::ToolRegistry registry;
    auto added = registry.add(std::make_unique<codeharness::ReadFileTool>());
    REQUIRE(added.has_value());

    codeharness::ToolRequest request;
    request.id = "test-tool-use-id";
    request.name = "read_file";
    request.input_json = R"({"path": "hello.txt"})";

    codeharness::ToolContext context;
    context.cwd = temp_dir;
    auto result = registry.execute(request, context);
    REQUIRE(result.has_value());
    CHECK(result->tool_use_id == "test-tool-use-id");
    CHECK(result->content == "hello from file");
    CHECK(result->is_error == false);

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("read_file rejects paths outside cwd")
{
    codeharness::ReadFileTool tool;

    codeharness::ToolRequest request;
    request.id = "tool-use-1";
    request.name = "read_file";
    request.input_json = R"({"path":"../outside.txt"})";

    codeharness::ToolContext context;
    context.cwd = std::filesystem::temp_directory_path();

    auto result = tool.execute(request, context);

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::InvalidArgument);
}

namespace
{

class ReadFileRequestProvider final : public codeharness::Provider
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
    auto added = tools.add(std::make_unique<codeharness::ReadFileTool>());
    REQUIRE(added.has_value());

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
    CHECK(result->messages[0].role == codeharness::Role::User); // messages[0] User"read hello.txt"
    CHECK(
        result->messages[1].role ==
        codeharness::Role::Assistant); // messages[1] Assistant ToolUseBlock: read_file({"path":"hello.txt"})
    CHECK(
        result->messages[2].role ==
        codeharness::Role::Tool); // messages[2] Tool ToolResultBlock: "hello from engine file"
    CHECK(
        result->messages[3].role ==
        codeharness::Role::Assistant); // messages[3] Assistant TextBlock: "hello from engine file"
}

TEST_CASE("echo provider streams text delta")
{
    codeharness::EchoProvider provider;

    std::vector<codeharness::Message> messages;
    messages.push_back(codeharness::make_text_message(codeharness::Role::User, "hello"));

    std::string streamed_text;
    bool finished = false;

    auto result = provider.stream(messages, [&](const codeharness::ProviderEvent& event) {
        if (auto delta = std::get_if<codeharness::AssistantTextDelta>(&event))
        {
            streamed_text += delta->text;
        }

        if (std::holds_alternative<codeharness::MessageFinished>(event))
        {
            finished = true;
        }
    });

    REQUIRE(result.has_value());
    CHECK(streamed_text == "hello");
    CHECK(finished);
}

TEST_CASE("engine streaming emits assistant text delta")
{
    codeharness::EchoProvider provider;
    codeharness::Engine engine{provider};

    codeharness::RunRequest request;
    request.prompt = "hello";
    request.options.max_turns = 1;

    std::string streamed_text;

    auto result = engine.run_streaming(request, [&](const codeharness::EngineEvent& event) {
        if (const auto *delta = std::get_if<codeharness::EngineAssistantTextDelta>(&event))
        {
            streamed_text += delta->text;
        }
    });

    REQUIRE(result.has_value());
    CHECK(streamed_text == "hello");
    CHECK(result->output_text == "hello");
}

namespace
{

class ToolDeltaBeforeStartProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message>, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        sink(
            codeharness::ToolUseInputDelta{
                .id = "tool-use-1",
                .input_json_delta = "{}",
            });
        sink(codeharness::MessageFinished{});

        return {};
    }
};

} // namespace

TEST_CASE("provider generate rejects tool input before tool start")
{
    ToolDeltaBeforeStartProvider provider;

    std::vector<codeharness::Message> messages;
    messages.push_back(codeharness::make_text_message(codeharness::Role::User, "hello"));

    auto result = provider.generate(std::span<const codeharness::Message>(messages));

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::Provider);
}

// WriteFileTool
TEST_CASE("write_file creates a new file")
{
    TempDir temp{"codeharness-write-file-test"};

    codeharness::WriteFileTool tool;

    codeharness::ToolRequest request;
    request.id = "test-1";
    request.name = "write_file";
    request.input_json = R"({"path":"output.txt","content":"hello world"})";

    codeharness::ToolContext context;
    context.cwd = temp.path;

    auto result = tool.execute(request, context);

    REQUIRE(result.has_value());
    CHECK(result->is_error == false);
    CHECK(result->tool_use_id == "test-1");

    auto file_path = temp.path / "output.txt";
    REQUIRE(std::filesystem::exists(file_path));

    // 验证文件内容
    REQUIRE(std::filesystem::exists(file_path));
    CHECK(read_file_text(file_path) == "hello world");
}

TEST_CASE("write_file creates parent directories by default")
{
    TempDir temp{"codeharness-write-file-nested-test"};

    codeharness::WriteFileTool tool;

    codeharness::ToolRequest request;
    request.id = "test-2";
    request.name = "write_file";
    request.input_json = R"({"path":"deep/nested/dir/file.txt","content":"nested content"})";

    codeharness::ToolContext context;
    context.cwd = temp.path;

    auto result = tool.execute(request, context);

    REQUIRE(result.has_value());
    CHECK(result->is_error == false);

    // 中间目录应该被自动创建
    auto file_path = temp.path / "deep" / "nested" / "dir" / "file.txt";
    REQUIRE(std::filesystem::exists(file_path));
}

TEST_CASE("write_file rejects paths outside cwd")
{
    codeharness::WriteFileTool tool;

    codeharness::ToolRequest request;
    request.id = "test-3";
    request.name = "write_file";
    request.input_json = R"({"path":"../outside.txt","content":"escape"})";

    codeharness::ToolContext context;
    context.cwd = std::filesystem::temp_directory_path();

    auto result = tool.execute(request, context);

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::InvalidArgument);
}

TEST_CASE("write_file rejects absolute paths")
{
    codeharness::WriteFileTool tool;

    codeharness::ToolRequest request;
    request.id = "test-4";
    request.name = "write_file";
    request.input_json = R"({"path":"/etc/passwd","content":"hacked"})";

    codeharness::ToolContext context;
    context.cwd = std::filesystem::temp_directory_path();

    auto result = tool.execute(request, context);

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::InvalidArgument);
}

TEST_CASE("write_file overwrites existing file")
{
    TempDir temp{"codeharness-write-overwrite-test"};

    // 先创建一个已有文件
    {
        std::ofstream file{temp.path / "existing.txt"};
        file << "old content";
    }

    codeharness::WriteFileTool tool;

    codeharness::ToolRequest request;
    request.id = "test-5";
    request.name = "write_file";
    request.input_json = R"({"path":"existing.txt","content":"new content"})";

    codeharness::ToolContext context;
    context.cwd = temp.path;

    auto result = tool.execute(request, context);

    REQUIRE(result.has_value());
    CHECK(result->is_error == false);
    // 结果应包含 "Overwrote" 字样
    CHECK(result->content.find("Overwrote") != std::string::npos);
    CHECK(read_file_text(temp.path / "existing.txt") == "new content");
}

TEST_CASE("write_file requires content field")
{
    codeharness::WriteFileTool tool;

    codeharness::ToolRequest request;
    request.id = "test-6";
    request.name = "write_file";
    // 缺少 content 字段
    request.input_json = R"({"path":"test.txt"})";

    codeharness::ToolContext context;
    context.cwd = std::filesystem::temp_directory_path();

    auto result = tool.execute(request, context);

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::InvalidArgument);
}

// PermissionChecker
TEST_CASE("permission checker allows read-only tools in default mode")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Default;

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("read_file", true, std::filesystem::path{"hello.txt"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Allow);
}

TEST_CASE("permission checker asks for mutating tools in default mode")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Default;

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("write_file", false, std::filesystem::path{"output.txt"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Ask);
}

TEST_CASE("permission checker denies mutating tools in plan mode")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::Plan;

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("write_file", false, std::filesystem::path{"output.txt"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Deny);
}

TEST_CASE("permission checker allows mutating tools in full_auto mode")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("write_file", false, std::filesystem::path{"output.txt"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Allow);
}

TEST_CASE("permission checker blocks sensitive paths even in full_auto mode")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("read_file", true, std::filesystem::path{".ssh/id_rsa"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Deny);
}

TEST_CASE("permission checker denied_tools wins over allowed_tools")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;
    settings.allowed_tools.push_back("write_file");
    settings.denied_tools.push_back("write_file");

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("write_file", false, std::filesystem::path{"output.txt"}, std::nullopt);

    CHECK(decision.action == codeharness::PermissionAction::Deny);
}

TEST_CASE("workspace path rejects escaping through nested relative path")
{
    codeharness::WriteFileTool tool;

    codeharness::ToolRequest request;
    request.id = "test-workspace-escape";
    request.name = "write_file";

    // nested/../../outside.txt 会向上跳两级，
    // 最终逃逸 cwd，所以必须被拒绝。
    request.input_json = R"({"path":"nested/../../outside.txt","content":"escape"})";

    codeharness::ToolContext context;
    context.cwd = std::filesystem::temp_directory_path();

    auto result = tool.execute(request, context);

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::InvalidArgument);
}