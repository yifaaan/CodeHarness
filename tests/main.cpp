#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "codeharness/core/message.h"
#include "codeharness/engine/engine.h"
#include "codeharness/provider/echo_provider.h"
#include "codeharness/tools/read_file_tool.h"
#include "codeharness/tools/tool_registry.h"
#include "codeharness/version.h"

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