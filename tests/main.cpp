#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "codeharness/core/message.h"
#include "codeharness/engine/engine.h"
#include "codeharness/provider/echo_provider.h"
#include "codeharness/provider/provider.h"
#include "codeharness/tools/read_file_tool.h"
#include "codeharness/tools/tool_registry.h"
#include "codeharness/version.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

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
    registry.add(std::make_unique<codeharness::ReadFileTool>());

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
    auto generate(std::span<const codeharness::Message> messages) -> codeharness::Result<codeharness::Message> override
    {
        // check history for a tool use request, and if found return a message with the tool result.
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
                    return codeharness::make_text_message(codeharness::Role::Assistant, result->content);
                }
            }
        }

        // otherwise return a message with a tool use request for read_file.
        codeharness::Message message;
        message.role = codeharness::Role::Assistant;
        message.content.emplace_back(
            codeharness::ToolUseBlock{
                .id = "tool-use-1",
                .name = "read_file",
                .input_json = R"({"path":"hello.txt"})",
            });

        return message;
    }
};

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

} // namespace