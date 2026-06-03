#include "codeharness/tools/grep_tool.h"

#include <glob/glob.h>
#include <re2/re2.h>
#include <nlohmann/json.hpp>
#include <nonstd/expected.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include "codeharness/core/assign.h"
#include "codeharness/core/json_parse.h"
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
    GrepInput parsed;

    if (auto r = assign(parsed.pattern, read_json_field<std::string>(input, "pattern", "grep")); !r)
    {
        return nonstd::make_unexpected(r.error());
    }

    if (auto r = assign(
            parsed.max_results,
            read_json_field<int, JsonFieldMode::optional_with_default>(input, "max_results", "grep", 200));
        !r)
    {
        return nonstd::make_unexpected(r.error());
    }

    auto path = read_json_field<std::string, JsonFieldMode::optional_if_valid>(input, "path");
    if (!path)
    {
        return nonstd::make_unexpected(path.error());
    }
    if (*path)
    {
        parsed.path = std::move(**path);
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

auto is_skipped_directory_name(const std::filesystem::path& path) -> bool
{
    const auto name = path.filename().string();

    return name == ".git" || name == "build";
}

auto is_under_skipped_directory(const std::filesystem::path& root, const std::filesystem::path& path) -> bool
{
    std::error_code error;
    const auto relative = std::filesystem::relative(path, root, error);
    if (error)
    {
        return false;
    }

    for (const auto& part : relative.parent_path())
    {
        if (is_skipped_directory_name(part))
        {
            return true;
        }
    }

    return false;
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

        if (RE2::PartialMatch(line, regex))
        {
            std::error_code rel_error;
            auto relative = std::filesystem::relative(file_path, workspace, rel_error);
            auto relative_path = rel_error ? file_path.generic_string() : relative.generic_string();

            results.push_back(
                nlohmann::json{
                    {"path", std::move(relative_path)},
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
    const auto matches = glob::rglob(directory.string() + "/**/*");

    for (const auto& match : matches)
    {
        if (static_cast<int>(results.size()) >= max_results)
        {
            break;
        }

        const auto entry_path = std::filesystem::path{match};
        if (is_under_skipped_directory(directory, entry_path))
        {
            continue;
        }

        search_file(workspace, entry_path, regex, max_results, results);
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

auto GrepTool::permission_target(const ToolRequest& request) const -> PermissionTarget
{
    return path_permission_target(request.parsed_input, "path");
}

auto GrepTool::execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>
{
    auto parsed = parse_grep_input(request.parsed_input);
    if (!parsed)
    {
        return fail<ToolResponse>(parsed.error().kind, parsed.error().message);
    }

    std::error_code ws_error;
    auto workspace = std::filesystem::weakly_canonical(context.cwd, ws_error);
    if (ws_error)
    {
        return fail<ToolResponse>(ErrorKind::Io, "failed to resolve workspace path: " + ws_error.message());
    }

    std::filesystem::path search_root = workspace;
    if (parsed->path && *parsed->path != ".")
    {
        auto resolved = resolve_workspace_path(workspace, *parsed->path);
        if (!resolved)
        {
            return fail<ToolResponse>(resolved.error().kind, resolved.error().message);
        }
        search_root = *resolved;
    }

    auto regex = std::make_unique<RE2>(parsed->pattern);
    if (!regex->ok())
    {
        return fail<ToolResponse>(ErrorKind::InvalidArgument, "invalid grep pattern: " + regex->error());
    }

    nlohmann::json results = nlohmann::json::array();

    if (std::filesystem::is_regular_file(search_root))
    {
        search_file(workspace, search_root, *regex, parsed->max_results, results);
    }
    else if (std::filesystem::is_directory(search_root))
    {
        auto search_result = search_directory(workspace, search_root, *regex, parsed->max_results, results);
        if (!search_result)
        {
            return fail<ToolResponse>(search_result.error().kind, search_result.error().message);
        }
    }
    else
    {
        return fail<ToolResponse>(ErrorKind::Io, "grep path is not a file or directory: " + search_root.string());
    }

    return ToolResponse{
        .tool_use_id = request.id,
        .content = results.dump(2),
        .is_error = false,
    };
}

} // namespace codeharness
