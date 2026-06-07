#include "test_support.h"

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

TEST_CASE("bash truncates large output without failing")
{
    codeharness::BashTool tool;

    codeharness::ToolRequest request;
    request.id = "large-bash-output";
    request.name = "bash";
#if defined(_WIN32)
    set_request_input(request, R"({"command":"for /L %i in (1,1,2000) do @echo 0123456789abcdef0123456789abcdef","timeout_seconds":5})");
#else
    set_request_input(request, R"({"command":"yes 0123456789abcdef0123456789abcdef | head -n 2000","timeout_seconds":5})");
#endif

    codeharness::ToolContext context;
    context.cwd = std::filesystem::current_path();

    auto result = tool.execute(request, context);

    REQUIRE(result.has_value());
    CHECK(result->is_error == false);
    CHECK(result->content.find("...[output truncated, too long]...") != std::string::npos);
    CHECK(result->content.size() < 13000);
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
