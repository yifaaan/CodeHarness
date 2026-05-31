#include "codeharness/tools/grep_tool.h"

#include <nlohmann/json.hpp>
#include <re2/re2.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include "codeharness/tools/workspace_path.h"

namespace codeharness
{

namespace
{

struct GrepInput
{
    std::string pattern;
    std::optional<std::filesystem::path> path;
    int max_results = 200;
};

// grep 的最小输入格式：
//   {
//     "pattern": "TODO|FIXME",   // 必填，RE2 正则表达式
//     "path": "src",             // 可选，文件或目录；不传则搜索整个 cwd
//     "max_results": 50           // 可选，避免一次返回太多内容
//   }
auto parse_grep_input(const nlohmann::json& input) -> Result<GrepInput>
{
    if (!input.contains("pattern") || !input["pattern"].is_string())
    {
        return fail<GrepInput>(ErrorKind::InvalidArgument, "grep requires string field: pattern");
    }

    if (input.contains("path") && !input["path"].is_string())
    {
        return fail<GrepInput>(ErrorKind::InvalidArgument, "grep path must be a string");
    }

    if (input.contains("max_results") && !input["max_results"].is_number_integer())
    {
        return fail<GrepInput>(ErrorKind::InvalidArgument, "grep max_results must be an integer");
    }

    GrepInput parsed;
    parsed.pattern = input["pattern"].get<std::string>();
    parsed.max_results = input.value("max_results", 200);

    if (input.contains("path"))
    {
        parsed.path = input["path"].get<std::string>();
    }

    if (parsed.pattern.empty())
    {
        return fail<GrepInput>(ErrorKind::InvalidArgument, "grep pattern is empty");
    }

    if (parsed.max_results <= 0)
    {
        return fail<GrepInput>(ErrorKind::InvalidArgument, "grep max_results must be positive");
    }

    constexpr int max_result_limit = 1000;
    if (parsed.max_results > max_result_limit)
    {
        parsed.max_results = max_result_limit;
    }

    return parsed;
}

// 用 RE2 编译正则表达式。
//
// 这里回答了“不要重复造轮子，也不要用 C API”的要求：
//   - xmake 负责导入 re2 包；
//   - 代码直接使用 RE2 这个 C++ 类；
//   - 不需要 pcre2_compile / pcre2_match 这类 C 风格资源管理。
//
// RE2 的一个重要特点：匹配时间是线性的，不支持会导致指数级回溯的特性。
// 对 agent harness 来说这很适合，因为模型可能传入任意 pattern，
// 我们更希望 grep 工具稳定、可预测，而不是追求最完整的 PCRE 语法。
auto compile_pattern(const std::string& pattern) -> Result<std::unique_ptr<RE2>>
{
    auto regex = std::make_unique<RE2>(pattern);
    if (!regex->ok())
    {
        return fail<std::unique_ptr<RE2>>(ErrorKind::InvalidArgument, "invalid grep pattern: " + regex->error());
    }

    return regex;
}

// 用已编译的 RE2 正则匹配一行文本。
//
// PartialMatch 表示“这一行里任意位置匹配即可”，这就是 grep 的常见语义：
//   pattern = "TODO"
//   line    = "// TODO: implement"
// 结果应当匹配，而不要求整行只等于 TODO。
auto matches_line(const RE2& regex, const std::string& line) -> bool
{
    return RE2::PartialMatch(line, regex);
}

auto canonical_workspace(const std::filesystem::path& cwd) -> Result<std::filesystem::path>
{
    std::error_code error;
    auto workspace = std::filesystem::weakly_canonical(cwd, error);
    if (error)
    {
        return fail<std::filesystem::path>(ErrorKind::Io, "failed to resolve workspace path: " + error.message());
    }

    return workspace;
}

auto resolve_search_root(const std::filesystem::path& workspace, const GrepInput& input) -> Result<std::filesystem::path>
{
    if (!input.path || *input.path == ".")
    {
        return workspace;
    }

    return resolve_workspace_path(workspace, *input.path);
}

auto is_skipped_directory(const std::filesystem::path& path) -> bool
{
    const auto name = path.filename().string();

    return name == ".git" || name == "build";
}

auto is_small_regular_file(const std::filesystem::path& path) -> bool
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
    {
        return false;
    }

    constexpr std::uintmax_t max_file_size = 1024U * 1024U;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
    {
        return false;
    }

    return size <= max_file_size;
}

auto relative_tool_path(const std::filesystem::path& workspace, const std::filesystem::path& file_path) -> std::string
{
    std::error_code error;
    const auto relative = std::filesystem::relative(file_path, workspace, error);

    if (error)
    {
        return file_path.generic_string();
    }

    return relative.generic_string();
}

auto search_file(
    const std::filesystem::path& workspace,
    const std::filesystem::path& file_path,
    const RE2& regex,
    int max_results,
    nlohmann::json& results) -> void
{
    if (!is_small_regular_file(file_path))
    {
        return;
    }

    std::ifstream file{file_path, std::ios::binary};
    if (!file)
    {
        return;
    }

    std::string line;
    int line_number = 1;

    while (static_cast<int>(results.size()) < max_results && std::getline(file, line))
    {
        // 简单二进制文件判断：文本文件中通常不会出现 '\0'。
        // 一旦发现 NUL 字节，就跳过整个文件，避免把二进制内容塞进工具结果。
        if (line.find('\0') != std::string::npos)
        {
            return;
        }

        if (matches_line(regex, line))
        {
            results.push_back(
                nlohmann::json{
                    {"path", relative_tool_path(workspace, file_path)},
                    {"line_number", line_number},
                    {"text", line},
                });
        }

        ++line_number;
    }
}

auto search_directory(
    const std::filesystem::path& workspace,
    const std::filesystem::path& directory,
    const RE2& regex,
    int max_results,
    nlohmann::json& results) -> Result<void>
{
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator{
        directory,
        std::filesystem::directory_options::skip_permission_denied,
        error};

    if (error)
    {
        return fail<void>(ErrorKind::Io, "failed to search directory: " + error.message());
    }

    const std::filesystem::recursive_directory_iterator end;

    while (iterator != end && static_cast<int>(results.size()) < max_results)
    {
        const auto& entry = *iterator;
        const auto entry_path = entry.path();

        if (entry.is_directory(error) && !error)
        {
            if (is_skipped_directory(entry_path))
            {
                iterator.disable_recursion_pending();
            }
        }
        else if (entry.is_regular_file(error) && !error)
        {
            search_file(workspace, entry_path, regex, max_results, results);
        }

        // 遍历某个文件失败时不要让整个 grep 失败。
        // 例如权限不够、文件被删除，都只影响当前 entry。
        error.clear();
        iterator.increment(error);
        if (error)
        {
            error.clear();
        }
    }

    return {};
}

} // namespace

auto GrepTool::name() const -> std::string
{
    return "grep";
}

auto GrepTool::description() const -> std::string
{
    return "Search file contents using an RE2 regular expression under the current workspace directory.";
}

auto GrepTool::is_read_only() const noexcept -> bool
{
    return true;
}

auto GrepTool::execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>
{
    nlohmann::json input;

    try
    {
        input = nlohmann::json::parse(request.input_json);
    }
    catch (const nlohmann::json::parse_error& error)
    {
        return fail<ToolResponse>(ErrorKind::InvalidArgument, error.what());
    }

    auto parsed = parse_grep_input(input);
    if (!parsed)
    {
        return fail<ToolResponse>(parsed.error().kind, parsed.error().message);
    }

    auto workspace = canonical_workspace(context.cwd);
    if (!workspace)
    {
        return fail<ToolResponse>(workspace.error().kind, workspace.error().message);
    }

    auto search_root = resolve_search_root(*workspace, *parsed);
    if (!search_root)
    {
        return fail<ToolResponse>(search_root.error().kind, search_root.error().message);
    }

    auto pattern = compile_pattern(parsed->pattern);
    if (!pattern)
    {
        return fail<ToolResponse>(pattern.error().kind, pattern.error().message);
    }

    nlohmann::json results = nlohmann::json::array();

    if (std::filesystem::is_regular_file(*search_root))
    {
        search_file(*workspace, *search_root, **pattern, parsed->max_results, results);
    }
    else if (std::filesystem::is_directory(*search_root))
    {
        auto search_result = search_directory(*workspace, *search_root, **pattern, parsed->max_results, results);
        if (!search_result)
        {
            return fail<ToolResponse>(search_result.error().kind, search_result.error().message);
        }
    }
    else
    {
        return fail<ToolResponse>(ErrorKind::Io, "grep path is not a file or directory: " + search_root->string());
    }

    return ToolResponse{
        .tool_use_id = request.id,
        .content = results.dump(2),
        .is_error = false,
    };
}

} // namespace codeharness
