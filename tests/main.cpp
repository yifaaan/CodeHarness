#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <git2.h>
#include <spdlog/spdlog.h>

namespace
{

// 测试运行时把 spdlog 调成 off，避免工具调用日志淹没 doctest 输出。
struct SpdlogQuieter
{
    SpdlogQuieter()
    {
        spdlog::set_level(spdlog::level::off);
    }
};

} // namespace

static SpdlogQuieter g_spdlog_quieter{};

#include "codeharness/core/message.h"
#include "codeharness/commands/command_registry.h"
#include "codeharness/engine/engine.h"
#include "codeharness/permissions/permission.h"
#include "codeharness/prompts/project_context.h"
#include "codeharness/prompts/system_prompt.h"
#include "codeharness/provider/echo_provider.h"
#include "codeharness/provider/provider.h"
#include "codeharness/skills/skill_loader.h"
#include "codeharness/skills/skill_registry.h"
#include "codeharness/tools/bash_tool.h"
#include "codeharness/tools/edit_file_tool.h"
#include "codeharness/tools/glob_tool.h"
#include "codeharness/tools/grep_tool.h"
#include "codeharness/tools/read_file_tool.h"
#include "codeharness/tools/skill_tool.h"
#include "codeharness/tools/tool_registry.h"
#include "codeharness/tools/write_file_tool.h"
#include "codeharness/version.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
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

// 测试 helper：同时设置 ToolRequest 的 input_json 和 parsed_input。
// production code 中 Engine 会通过 parse_tool_request_input 填充 parsed_input；
// 直接调用 Tool 的测试需要自己 parse 一次。
auto set_request_input(codeharness::ToolRequest& request, std::string input_json) -> void
{
    request.input_json = input_json;
    request.parsed_input = nlohmann::json::parse(input_json);
}

auto read_file_text(const std::filesystem::path& path) -> std::string
{
    std::ifstream file{path, std::ios::binary};

    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

struct GitTestRuntime
{
    GitTestRuntime()
    {
        git_libgit2_init();
    }

    ~GitTestRuntime()
    {
        git_libgit2_shutdown();
    }

    GitTestRuntime(const GitTestRuntime&) = delete;
    auto operator=(const GitTestRuntime&) -> GitTestRuntime& = delete;
};

constexpr auto kGitTestRepositoryDeleter = [](git_repository *repository) noexcept {
    git_repository_free(repository);
};

auto init_git_repository_with_head(const std::filesystem::path& repo, std::string_view branch) -> void
{
    GitTestRuntime runtime;

    git_repository *raw_repository = nullptr;
    REQUIRE(git_repository_init(&raw_repository, repo.string().c_str(), 0) == 0);

    const std::unique_ptr<git_repository, decltype(kGitTestRepositoryDeleter)> repository{
        raw_repository,
        kGitTestRepositoryDeleter,
    };
    const auto head_name = std::string{"refs/heads/"} + std::string{branch};
    REQUIRE(git_repository_set_head(repository.get(), head_name.c_str()) == 0);
}

class CapturingProvider final : public codeharness::Provider
{
public:
    std::vector<codeharness::Message> seen_messages;

    auto stream(std::span<const codeharness::Message> messages, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        seen_messages.assign(messages.begin(), messages.end());
        sink(codeharness::AssistantTextDelta{"ok"});
        sink(codeharness::MessageFinished{});
        return {};
    }
};

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
    messages.push_back(codeharness::make_text_message(codeharness::Role::System, "system rules"));
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

TEST_CASE("engine prepends system prompt when provided")
{
    CapturingProvider provider;
    codeharness::Engine engine{provider};

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
    registry.add(std::make_unique<codeharness::ReadFileTool>());

    codeharness::ToolRequest request;
    request.id = "test-tool-use-id";
    request.name = "read_file";
    set_request_input(request, R"({"path": "hello.txt"})");

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
    set_request_input(request, R"({"path":"../outside.txt"})");

    codeharness::ToolContext context;
    context.cwd = std::filesystem::temp_directory_path();

    auto result = tool.execute(request, context);

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::InvalidArgument);
}

TEST_CASE("read_file returns requested line range")
{
    TempDir temp{"codeharness-read-file-range-test"};

    {
        std::ofstream file{temp.path / "lines.txt", std::ios::binary};
        file << "line 1\n";
        file << "line 2\n";
        file << "line 3\n";
        file << "line 4\n";
    }

    codeharness::ReadFileTool tool;

    codeharness::ToolRequest request;
    request.id = "tool-use-range";
    request.name = "read_file";
    set_request_input(request, R"({"path":"lines.txt","offset":1,"limit":2})");

    codeharness::ToolContext context;
    context.cwd = temp.path;

    auto result = tool.execute(request, context);

    REQUIRE(result.has_value());
    CHECK(result->content == "line 2\nline 3\n");
    CHECK(result->is_error == false);
}

TEST_CASE("glob_tool returns relative paths under search root")
{
    TempDir temp{"codeharness-glob-test"};

    std::filesystem::create_directories(temp.path / "src" / "feature");
    {
        std::ofstream{temp.path / "src" / "main.cpp"};
        std::ofstream{temp.path / "src" / "feature" / "x.cpp"};
    }

    codeharness::GlobTool tool;

    codeharness::ToolRequest request;
    request.id = "glob-use-1";
    request.name = "glob";
    set_request_input(request, R"({"pattern":"**/*.cpp","path":"src"})");

    codeharness::ToolContext context;
    context.cwd = temp.path;

    auto result = tool.execute(request, context);

    REQUIRE(result.has_value());
    CHECK(result->is_error == false);

    const auto content = result->content;
    CHECK(content.find("\"main.cpp\"") != std::string::npos);
    CHECK(content.find("\"feature/x.cpp\"") != std::string::npos);
    CHECK(content.find(temp.path.string()) == std::string::npos);
}

TEST_CASE("grep returns regex matches with file and line")
{
    TempDir temp{"codeharness-grep-test"};

    std::filesystem::create_directories(temp.path / "src");
    std::filesystem::create_directories(temp.path / ".git");

    {
        std::ofstream file{temp.path / "src" / "main.cpp", std::ios::binary};
        file << "int main() {\n";
        file << "    // TODO: wire provider\n";
        file << "    return 0;\n";
        file << "}\n";
    }

    {
        std::ofstream file{temp.path / ".git" / "ignored.txt", std::ios::binary};
        file << "TODO: should not be searched\n";
    }

    codeharness::GrepTool tool;

    codeharness::ToolRequest request;
    request.id = "grep-use-1";
    request.name = "grep";
    set_request_input(request, R"({"pattern":"TODO:.*provider","path":".","max_results":10})");

    codeharness::ToolContext context;
    context.cwd = temp.path;

    auto result = tool.execute(request, context);

    REQUIRE(result.has_value());
    CHECK(result->is_error == false);
    CHECK(result->content.find("src/main.cpp") != std::string::npos);
    CHECK(result->content.find("\"line_number\": 2") != std::string::npos);
    CHECK(result->content.find("TODO: wire provider") != std::string::npos);
    CHECK(result->content.find("ignored.txt") == std::string::npos);
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
    set_request_input(request, R"({"path":"output.txt","content":"hello world"})");

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
    set_request_input(request, R"({"path":"deep/nested/dir/file.txt","content":"nested content"})");

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
    set_request_input(request, R"({"path":"../outside.txt","content":"escape"})");

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
    set_request_input(request, R"({"path":"/etc/passwd","content":"hacked"})");

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
    set_request_input(request, R"({"path":"existing.txt","content":"new content"})");

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
    set_request_input(request, R"({"path":"test.txt"})");

    codeharness::ToolContext context;
    context.cwd = std::filesystem::temp_directory_path();

    auto result = tool.execute(request, context);

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::InvalidArgument);
}

TEST_CASE("edit_file replaces a unique string")
{
    TempDir temp{"codeharness-edit-file-test"};

    {
        std::ofstream file{temp.path / "note.txt", std::ios::binary};
        file << "alpha beta gamma\n";
    }

    codeharness::EditFileTool tool;

    codeharness::ToolRequest request;
    request.id = "edit-1";
    request.name = "edit_file";
    set_request_input(request, R"({"path":"note.txt","old_string":"beta","new_string":"BETA"})");

    codeharness::ToolContext context;
    context.cwd = temp.path;

    auto result = tool.execute(request, context);

    REQUIRE(result.has_value());
    CHECK(result->is_error == false);
    CHECK(result->tool_use_id == "edit-1");
    CHECK(result->content.find("1 replacement") != std::string::npos);
    CHECK(read_file_text(temp.path / "note.txt") == "alpha BETA gamma\n");
}

TEST_CASE("edit_file rejects multiple matches by default")
{
    TempDir temp{"codeharness-edit-file-multiple-test"};

    {
        std::ofstream file{temp.path / "words.txt", std::ios::binary};
        file << "red blue red\n";
    }

    codeharness::EditFileTool tool;

    codeharness::ToolRequest request;
    request.id = "edit-2";
    request.name = "edit_file";
    set_request_input(request, R"({"path":"words.txt","old_string":"red","new_string":"green"})");

    codeharness::ToolContext context;
    context.cwd = temp.path;

    auto result = tool.execute(request, context);

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::InvalidArgument);
    CHECK(read_file_text(temp.path / "words.txt") == "red blue red\n");
}

TEST_CASE("edit_file can replace all matches explicitly")
{
    TempDir temp{"codeharness-edit-file-replace-all-test"};

    {
        std::ofstream file{temp.path / "words.txt", std::ios::binary};
        file << "red blue red\n";
    }

    codeharness::EditFileTool tool;

    codeharness::ToolRequest request;
    request.id = "edit-3";
    request.name = "edit_file";
    set_request_input(request, R"({"path":"words.txt","old_string":"red","new_string":"green","replace_all":true})");

    codeharness::ToolContext context;
    context.cwd = temp.path;

    auto result = tool.execute(request, context);

    REQUIRE(result.has_value());
    CHECK(result->is_error == false);
    CHECK(result->content.find("2 replacements") != std::string::npos);
    CHECK(read_file_text(temp.path / "words.txt") == "green blue green\n");
}

TEST_CASE("edit_file rejects paths outside cwd")
{
    codeharness::EditFileTool tool;

    codeharness::ToolRequest request;
    request.id = "edit-4";
    request.name = "edit_file";
    set_request_input(request, R"({"path":"../outside.txt","old_string":"old","new_string":"new"})");

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

TEST_CASE("permission checker blocks dangerous commands even in full_auto mode")
{
    codeharness::PermissionSettings settings;
    settings.mode = codeharness::PermissionMode::FullAuto;

    codeharness::PermissionChecker checker{settings};

    auto decision = checker.evaluate("bash", false, std::nullopt, std::string{"printf 'rm -rf /'"});

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
    set_request_input(request, R"({"path":"nested/../../outside.txt","content":"escape"})");

    codeharness::ToolContext context;
    context.cwd = std::filesystem::temp_directory_path();

    auto result = tool.execute(request, context);

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::InvalidArgument);
}

TEST_CASE("tools expose permission targets for engine checks")
{
    codeharness::ReadFileTool read_tool;
    codeharness::ToolRequest read_request;
    read_request.id = "target-1";
    read_request.name = "read_file";
    set_request_input(read_request, R"({"path":"secrets.txt"})");

    auto read_target = read_tool.permission_target(read_request);
    REQUIRE(read_target.path.has_value());
    CHECK(read_target.path->generic_string() == "secrets.txt");
    CHECK(!read_target.command.has_value());

    codeharness::BashTool bash_tool;
    codeharness::ToolRequest bash_request;
    bash_request.id = "target-2";
    bash_request.name = "bash";
    set_request_input(bash_request, R"({"command":"printf 'rm -rf /'"})");

    auto bash_target = bash_tool.permission_target(bash_request);
    REQUIRE(bash_target.command.has_value());
    CHECK(*bash_target.command == "printf 'rm -rf /'");
    CHECK(!bash_target.path.has_value());
}

TEST_CASE("read-only metadata works through base tool interface")
{
    const std::unique_ptr<codeharness::Tool> read_tool = std::make_unique<codeharness::ReadFileTool>();

    const std::unique_ptr<codeharness::Tool> write_tool = std::make_unique<codeharness::WriteFileTool>();

    const std::unique_ptr<codeharness::Tool> edit_tool = std::make_unique<codeharness::EditFileTool>();

    CHECK(read_tool->is_read_only() == true);
    CHECK(write_tool->is_read_only() == false);
    CHECK(edit_tool->is_read_only() == false);
}

// 请求 write_file 的 mock provider。
// 第一次调用时发出 ToolUseStarted + InputDelta + Finished，
// 第二次调用（拿到 tool result 后）输出一段文本作为最终回复。
namespace
{

class WriteFileRequestProvider final : public codeharness::Provider
{
public:
    auto stream(std::span<const codeharness::Message> messages, const codeharness::ProviderEventSink& sink)
        -> codeharness::Result<void> override
    {
        // 如果已经有 Tool 消息，说明工具已经执行完了，
        // 直接输出结果并结束。
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
                    // 把工具的结果当作最终回复返回给 engine
                    sink(codeharness::AssistantTextDelta{result->content});
                    sink(codeharness::MessageFinished{});
                    return {};
                }
            }
        }

        // 第一次调用：请求写文件
        sink(
            codeharness::ToolUseStarted{
                .id = "tool-use-1",
                .name = "write_file",
            });
        sink(
            codeharness::ToolUseInputDelta{
                .id = "tool-use-1",
                .input_json_delta = R"({"path":"output.txt","content":"hello"})",
            });
        sink(codeharness::ToolUseFinished{.id = "tool-use-1"});
        sink(codeharness::MessageFinished{});

        return {};
    }
};

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
    codeharness::Engine engine{provider, tools, checker};

    codeharness::RunRequest request;
    request.prompt = "write hello.txt";
    request.options.max_turns = 3;

    auto result = engine.run(request);

    // engine 不会崩溃，但最终输出应包含权限确认信息（Ask → 无 UI → 当做拒绝）
    REQUIRE(result.has_value());
    CHECK(result->output_text.find("permission confirmation required") != std::string::npos);
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
    codeharness::Engine engine{provider, tools, checker};

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
    codeharness::Engine engine{provider, tools, checker};

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
    codeharness::Engine engine{provider, tools, checker};

    codeharness::RunRequest request;
    request.prompt = "run a command";
    request.options.max_turns = 3;

    auto result = engine.run(request);

    REQUIRE(result.has_value());
    CHECK(result->output_text.find("permission denied: dangerous command is blocked") != std::string::npos);
}

TEST_CASE("project context loader reads AGENTS and CLAUDE from parent to child")
{
    TempDir temp{"codeharness-project-context-order-test"};

    const auto repo = temp.path / "repo";
    const auto child = repo / "src" / "feature";
    std::filesystem::create_directories(child);
    std::filesystem::create_directories(repo / ".git");

    {
        std::ofstream file{temp.path / "AGENTS.md", std::ios::binary};
        file << "outside";
    }

    {
        std::ofstream file{repo / "AGENTS.md", std::ios::binary};
        file << "repo agents";
    }

    {
        std::ofstream file{repo / "CLAUDE.md", std::ios::binary};
        file << "repo claude";
    }

    {
        std::ofstream file{child / "AGENTS.md", std::ios::binary};
        file << "child agents";
    }

    auto files = codeharness::load_project_context_files(child);

    REQUIRE(files.has_value());
    REQUIRE(files->size() == 3);
    CHECK(files->at(0).path == repo / "AGENTS.md");
    CHECK(files->at(0).content == "repo agents");
    CHECK(files->at(1).path == repo / "CLAUDE.md");
    CHECK(files->at(1).content == "repo claude");
    CHECK(files->at(2).path == child / "AGENTS.md");
    CHECK(files->at(2).content == "child agents");
}

TEST_CASE("project context loader applies a total character budget")
{
    TempDir temp{"codeharness-project-context-budget-test"};

    const auto repo = temp.path / "repo";
    const auto child = repo / "src";
    std::filesystem::create_directories(child);
    std::filesystem::create_directories(repo / ".git");

    {
        std::ofstream file{repo / "AGENTS.md", std::ios::binary};
        file << "abcdef";
    }

    {
        std::ofstream file{child / "AGENTS.md", std::ios::binary};
        file << "ghijkl";
    }

    codeharness::ProjectContextOptions options;
    options.file_names = {"AGENTS.md"};
    options.max_total_chars = 8;

    auto files = codeharness::load_project_context_files(child, options);

    REQUIRE(files.has_value());
    REQUIRE(files->size() == 2);
    CHECK(files->at(0).path == repo / "AGENTS.md");
    CHECK(files->at(0).content == "abcdef");
    CHECK(files->at(1).path == child / "AGENTS.md");
    CHECK(files->at(1).content == "gh");
}

TEST_CASE("system prompt builder includes environment skills commands context and memory")
{
    TempDir temp{"codeharness-system-prompt-builder-test"};

    const auto repo = temp.path / "repo";
    const auto child = repo / "src";
    std::filesystem::create_directories(child);
    init_git_repository_with_head(repo, "main");

    codeharness::SkillDefinition review_skill;
    review_skill.name = "review";
    review_skill.description = "Review code.";
    review_skill.source = "bundled";

    codeharness::SkillDefinition hidden_skill;
    hidden_skill.name = "hidden";
    hidden_skill.description = "Hidden skill.";
    hidden_skill.source = "user";
    hidden_skill.disable_model_invocation = true;

    const auto noop_handler = [](std::string_view) -> codeharness::Result<codeharness::CommandResult> {
        return codeharness::CommandResult{};
    };

    codeharness::PromptBuildRequest request;
    request.cwd = child;
    request.latest_user_prompt = "review this";
    request.available_skills = {review_skill, hidden_skill};
    request.available_commands = {
        codeharness::SlashCommand{.name = "skills", .description = "List loaded skills.", .handler = noop_handler},
        codeharness::SlashCommand{.name = "review", .description = "Invoke the review skill.", .handler = noop_handler},
    };
    request.project_context_files = {
        codeharness::ContextFile{.path = repo / "AGENTS.md", .content = "repo agents"},
        codeharness::ContextFile{.path = child / "AGENTS.md", .content = "child agents"},
    };
    request.relevant_memories = {codeharness::RelevantMemory{.title = "Build Notes", .content = "Use xmake."}};
    request.permission_mode = codeharness::PermissionMode::Default;

    auto prompt = codeharness::SystemPromptBuilder{}.build(request);

    REQUIRE(prompt.has_value());
    CHECK(prompt->find("You are CodeHarness") != std::string::npos);
    CHECK(prompt->find("# Environment") != std::string::npos);
    CHECK(prompt->find("- Working directory: " + std::filesystem::weakly_canonical(child).string()) !=
          std::string::npos);
    CHECK(prompt->find("- Date: ") != std::string::npos);
    CHECK(prompt->find("- Git repository: yes") != std::string::npos);
    CHECK(prompt->find("- Git branch: main") != std::string::npos);
    CHECK(prompt->find("- review [bundled]: Review code.") != std::string::npos);
    CHECK(prompt->find("Hidden skill.") == std::string::npos);
    CHECK(prompt->find("- /skills: List loaded skills.") != std::string::npos);
    CHECK(prompt->find("- /review: Invoke the review skill.") != std::string::npos);
    CHECK(prompt->find("repo agents") < prompt->find("child agents"));
    CHECK(prompt->find("# Relevant Memories") != std::string::npos);
    CHECK(prompt->find("Use xmake.") != std::string::npos);
}

TEST_CASE("skill markdown parser reads frontmatter metadata")
{
    auto skill = codeharness::parse_skill_markdown(
        "review",
        "---\n"
        "name: Code Review\n"
        "description: Review code carefully.\n"
        "aliases:\n"
        "  - review\n"
        "  - inspect\n"
        "user-invocable: no\n"
        "disable-model-invocation: yes\n"
        "model: careful-model\n"
        "argument-hint: FILE_OR_DIFF\n"
        "---\n"
        "# Review\n"
        "Instructions.\n",
        "user");

    CHECK(skill.name == "Code Review");
    CHECK(skill.description == "Review code carefully.");
    CHECK(skill.source == "user");
    CHECK(skill.aliases == std::vector<std::string>{"review", "inspect"});
    CHECK(skill.user_invocable == false);
    CHECK(skill.disable_model_invocation == true);
    CHECK(skill.model == "careful-model");
    CHECK(skill.argument_hint == "FILE_OR_DIFF");
}

TEST_CASE("skill markdown parser falls back to heading and first body line")
{
    auto skill = codeharness::parse_skill_markdown("debug", "# Debugging\n\nDiagnose failures methodically.\n");

    CHECK(skill.name == "Debugging");
    CHECK(skill.description == "Diagnose failures methodically.");
}

TEST_CASE("skill registry supports aliases and later overrides")
{
    codeharness::SkillRegistry registry;
    registry.register_skill(
        codeharness::SkillDefinition{
            .name = "review",
            .description = "bundled",
            .content = "bundled content",
            .source = "bundled",
            .aliases = {"inspect"},
        });
    registry.register_skill(
        codeharness::SkillDefinition{
            .name = "review",
            .description = "user",
            .content = "user content",
            .source = "user",
        });

    REQUIRE(registry.get("review") != nullptr);
    CHECK(registry.get("review")->content == "user content");
    REQUIRE(registry.get("inspect") != nullptr);
    CHECK(registry.get("inspect")->content == "bundled content");
}

TEST_CASE("project skill closest to cwd overrides parent skill")
{
    TempDir temp{"codeharness-project-skill-order-test"};

    const auto repo = temp.path / "repo";
    const auto child = repo / "src" / "feature";
    const auto repo_skill = repo / ".agents" / "skills" / "review";
    const auto child_skill = child / ".agents" / "skills" / "review";
    std::filesystem::create_directories(repo / ".git");
    std::filesystem::create_directories(repo_skill);
    std::filesystem::create_directories(child_skill);

    {
        std::ofstream file{repo_skill / "SKILL.md", std::ios::binary};
        file << "# Review\n\nparent instructions\n";
    }

    {
        std::ofstream file{child_skill / "SKILL.md", std::ios::binary};
        file << "# Review\n\nchild instructions\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_user_skills = false;
    auto registry = codeharness::load_skill_registry(child, std::move(options));

    REQUIRE(registry.has_value());
    REQUIRE(registry->get("review") != nullptr);
    CHECK(registry->get("review")->content.find("child instructions") != std::string::npos);
}

TEST_CASE("skill registry loads bundled skills by default")
{
    TempDir temp{"codeharness-bundled-skill-default-test"};

    codeharness::SkillLoadOptions options;
    options.load_default_user_skills = false;
    options.allow_project_skills = false;

    auto registry = codeharness::load_skill_registry(temp.path, std::move(options));

    REQUIRE(registry.has_value());
    REQUIRE(registry->get("commit") != nullptr);
    REQUIRE(registry->get("plan") != nullptr);
    REQUIRE(registry->get("review") != nullptr);
    REQUIRE(registry->get("debug") != nullptr);
    REQUIRE(registry->get("diagnose") != nullptr);
    REQUIRE(registry->get("simplify") != nullptr);
    REQUIRE(registry->get("skill-creator") != nullptr);
    REQUIRE(registry->get("test") != nullptr);
    CHECK(registry->get("review")->source == "bundled");
    CHECK(registry->get("skill-creator")->description.find("Create, improve, and verify") != std::string::npos);
}

TEST_CASE("user skill overrides bundled skill")
{
    TempDir temp{"codeharness-user-overrides-bundled-skill-test"};
    const auto skill_dir = temp.path / "skills" / "review";
    std::filesystem::create_directories(skill_dir);

    {
        std::ofstream file{skill_dir / "SKILL.md", std::ios::binary};
        file << "---\n"
             << "name: review\n"
             << "description: local review skill\n"
             << "---\n"
             << "# Review\n\n"
             << "user review instructions\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_user_skills = false;
    options.allow_project_skills = false;
    options.user_skill_dirs = {temp.path / "skills"};

    auto registry = codeharness::load_skill_registry(temp.path, std::move(options));

    REQUIRE(registry.has_value());
    REQUIRE(registry->get("review") != nullptr);
    CHECK(registry->get("review")->source == "user");
    CHECK(registry->get("review")->content.find("user review instructions") != std::string::npos);
}

TEST_CASE("command registry resolves slash command names arguments and aliases")
{
    codeharness::CommandRegistry registry;
    registry.register_command(
        codeharness::SlashCommand{
            .name = "skills",
            .description = "list skills",
            .handler = [](std::string_view args) -> codeharness::Result<codeharness::CommandResult> {
                return codeharness::CommandResult{.message = std::string{args}};
            },
            .aliases = {"s"},
        });

    auto full = registry.lookup("/skills extra args");
    REQUIRE(full.command != nullptr);
    CHECK(full.command->name == "skills");
    CHECK(full.args == "extra args");

    auto alias = registry.lookup("/s");
    REQUIRE(alias.command != nullptr);
    CHECK(alias.command == full.command);

    auto missing = registry.lookup("/missing");
    CHECK(missing.command == nullptr);
}

TEST_CASE("skills command lists bundled user and project skills")
{
    TempDir temp{"codeharness-skills-command-list-test"};

    const auto repo = temp.path / "repo";
    const auto child = repo / "src";
    const auto user_root = temp.path / "user-skills";
    const auto user_skill = user_root / "user-only";
    const auto project_skill = child / ".agents" / "skills" / "project-only";
    std::filesystem::create_directories(repo / ".git");
    std::filesystem::create_directories(child);
    std::filesystem::create_directories(user_skill);
    std::filesystem::create_directories(project_skill);

    {
        std::ofstream file{user_skill / "SKILL.md", std::ios::binary};
        file << "---\n"
             << "name: user-only\n"
             << "description: user skill description\n"
             << "---\n"
             << "# user-only\n";
    }

    {
        std::ofstream file{project_skill / "SKILL.md", std::ios::binary};
        file << "---\n"
             << "name: project-only\n"
             << "description: project skill description\n"
             << "---\n"
             << "# project-only\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_user_skills = false;
    options.user_skill_dirs = {user_root};
    auto skills = codeharness::load_skill_registry(child, std::move(options));
    REQUIRE(skills.has_value());

    auto commands = codeharness::build_builtin_command_registry(*skills);
    auto result = codeharness::execute_slash_command(commands, "/skills ignored arguments");

    REQUIRE(result.has_value());
    REQUIRE(result->message.has_value());
    CHECK(result->message->find("- commit [bundled]:") != std::string::npos);
    CHECK(result->message->find("- plan [bundled]:") != std::string::npos);
    CHECK(result->message->find("- user-only [user]: user skill description") != std::string::npos);
    CHECK(result->message->find("- project-only [project]: project skill description") != std::string::npos);
}

TEST_CASE("unknown slash command returns a clear error")
{
    codeharness::CommandRegistry registry;

    auto result = codeharness::execute_slash_command(registry, "/missing command");

    REQUIRE(!result.has_value());
    CHECK(result.error().kind == codeharness::ErrorKind::InvalidArgument);
    CHECK(result.error().message == "unknown command: /missing");
}

TEST_CASE("bundled user invocable skill registers as slash command")
{
    TempDir temp{"codeharness-bundled-skill-command-test"};

    codeharness::SkillLoadOptions options;
    options.load_default_user_skills = false;
    options.allow_project_skills = false;
    auto skills = codeharness::load_skill_registry(temp.path, std::move(options));
    REQUIRE(skills.has_value());

    auto commands = codeharness::build_builtin_command_registry(*skills);
    auto result = codeharness::execute_slash_command(commands, "/skill-creator create a deployment skill");

    REQUIRE(result.has_value());
    CHECK(!result->message.has_value());
    REQUIRE(result->submit_prompt.has_value());
    CHECK(result->submit_prompt->find("Base directory for this skill:") != std::string::npos);
    CHECK(result->submit_prompt->find("# skill-creator") != std::string::npos);
    CHECK(result->submit_prompt->find("Arguments: create a deployment skill") != std::string::npos);
}

TEST_CASE("skill slash command replaces argument placeholders and skill dir")
{
    TempDir temp{"codeharness-skill-command-placeholders-test"};
    const auto skill_dir = temp.path / "skills" / "deploy";
    std::filesystem::create_directories(skill_dir);

    {
        std::ofstream file{skill_dir / "SKILL.md", std::ios::binary};
        file << "---\n"
             << "description: Deploy workflow.\n"
             << "---\n\n"
             << "# Deploy\n\n"
             << "plain: $ARGUMENTS\n"
             << "braced: ${ARGUMENTS}\n"
             << "dir: ${CLAUDE_SKILL_DIR}\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_bundled_skills = false;
    options.load_default_user_skills = false;
    options.allow_project_skills = false;
    options.user_skill_dirs = {temp.path / "skills"};
    auto skills = codeharness::load_skill_registry(temp.path, std::move(options));
    REQUIRE(skills.has_value());

    auto commands = codeharness::build_builtin_command_registry(*skills);
    auto result = codeharness::execute_slash_command(commands, "/deploy staging");

    REQUIRE(result.has_value());
    REQUIRE(result->submit_prompt.has_value());
    CHECK(result->submit_prompt->find("plain: staging") != std::string::npos);
    CHECK(result->submit_prompt->find("braced: staging") != std::string::npos);
    CHECK(result->submit_prompt->find("dir: " + skill_dir.string()) != std::string::npos);
    CHECK(result->submit_prompt->find("Arguments: staging") == std::string::npos);
}

TEST_CASE("project skill slash command appends arguments when no placeholder exists")
{
    TempDir temp{"codeharness-project-skill-command-args-test"};
    const auto repo = temp.path / "repo";
    const auto child = repo / "src";
    const auto skill_dir = child / ".agents" / "skills" / "shipit";
    std::filesystem::create_directories(repo / ".git");
    std::filesystem::create_directories(skill_dir);

    {
        std::ofstream file{skill_dir / "SKILL.md", std::ios::binary};
        file << "---\n"
             << "description: Ship workflow.\n"
             << "---\n\n"
             << "# Shipit\n\n"
             << "Ship this repo.\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_bundled_skills = false;
    options.load_default_user_skills = false;
    auto skills = codeharness::load_skill_registry(child, std::move(options));
    REQUIRE(skills.has_value());

    auto commands = codeharness::build_builtin_command_registry(*skills);
    auto result = codeharness::execute_slash_command(commands, "/shipit now");

    REQUIRE(result.has_value());
    REQUIRE(result->submit_prompt.has_value());
    CHECK(result->submit_prompt->find("Base directory for this skill: " + skill_dir.string()) != std::string::npos);
    CHECK(result->submit_prompt->find("# Shipit") != std::string::npos);
    CHECK(result->submit_prompt->find("Arguments: now") != std::string::npos);
}

TEST_CASE("non user invocable skill is not registered as slash command")
{
    TempDir temp{"codeharness-hidden-skill-command-test"};
    const auto skill_dir = temp.path / "skills" / "hidden";
    std::filesystem::create_directories(skill_dir);

    {
        std::ofstream file{skill_dir / "SKILL.md", std::ios::binary};
        file << "---\n"
             << "description: Hidden workflow.\n"
             << "user-invocable: false\n"
             << "---\n\n"
             << "# Hidden\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_bundled_skills = false;
    options.load_default_user_skills = false;
    options.allow_project_skills = false;
    options.user_skill_dirs = {temp.path / "skills"};
    auto skills = codeharness::load_skill_registry(temp.path, std::move(options));
    REQUIRE(skills.has_value());

    auto commands = codeharness::build_builtin_command_registry(*skills);
    auto result = codeharness::execute_slash_command(commands, "/hidden");

    REQUIRE(!result.has_value());
    CHECK(result.error().message == "unknown command: /hidden");
}

TEST_CASE("disable model invocation skill remains user slash invocable")
{
    TempDir temp{"codeharness-disable-model-skill-command-test"};
    const auto skill_dir = temp.path / "skills" / "deploy";
    std::filesystem::create_directories(skill_dir);

    {
        std::ofstream file{skill_dir / "SKILL.md", std::ios::binary};
        file << "---\n"
             << "description: Deploy workflow.\n"
             << "disable-model-invocation: true\n"
             << "model: gpt-5.4\n"
             << "---\n\n"
             << "# Deploy\n\n"
             << "$ARGUMENTS\n";
    }

    codeharness::SkillLoadOptions options;
    options.load_default_bundled_skills = false;
    options.load_default_user_skills = false;
    options.allow_project_skills = false;
    options.user_skill_dirs = {temp.path / "skills"};
    auto skills = codeharness::load_skill_registry(temp.path, std::move(options));
    REQUIRE(skills.has_value());

    auto commands = codeharness::build_builtin_command_registry(*skills);
    auto result = codeharness::execute_slash_command(commands, "/deploy staging");

    REQUIRE(result.has_value());
    CHECK(result->submit_model == "gpt-5.4");
    REQUIRE(result->submit_prompt.has_value());
    CHECK(result->submit_prompt->find("staging") != std::string::npos);
}

TEST_CASE("skill tool returns content and reports model invocation restrictions")
{
    codeharness::SkillRegistry registry;
    registry.register_skill(
        codeharness::SkillDefinition{
            .name = "review",
            .description = "review",
            .content = "review instructions",
            .source = "bundled",
        });
    registry.register_skill(
        codeharness::SkillDefinition{
            .name = "release",
            .description = "release",
            .content = "release instructions",
            .source = "user",
            .command_name = "release",
            .disable_model_invocation = true,
        });

    codeharness::SkillTool tool{registry};
    codeharness::ToolContext context;

    codeharness::ToolRequest review_request;
    review_request.id = "skill-review";
    review_request.name = "skill";
    set_request_input(review_request, R"({"name":"review"})");

    auto review = tool.execute(review_request, context);

    REQUIRE(review.has_value());
    CHECK(review->content == "review instructions");
    CHECK(review->is_error == false);

    codeharness::ToolRequest release_request;
    release_request.id = "skill-release";
    release_request.name = "skill";
    set_request_input(release_request, R"({"name":"release"})");

    auto release = tool.execute(release_request, context);

    REQUIRE(release.has_value());
    CHECK(release->content == "Skill release can only be invoked by the user as /release.");
    CHECK(release->is_error == true);
}
