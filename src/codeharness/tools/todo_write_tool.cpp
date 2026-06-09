#include "codeharness/tools/todo_write_tool.h"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <nonstd/expected.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "codeharness/core/assign.h"
#include "codeharness/core/json_parse.h"
#include "codeharness/core/result.h"
#include "codeharness/core/strings.h"
#include "codeharness/tools/text_file.h"
#include "codeharness/tools/workspace_path.h"

namespace codeharness
{

namespace
{

struct TodoWriteInput
{
    std::string item;              // TODO 项文本
    bool checked = false;          // 是否完成
    std::filesystem::path path = "TODO.md"; // 文件路径（相对于 cwd）
};

auto parse_todo_write_input(const nlohmann::json& input) -> Result<TodoWriteInput>
{
    TodoWriteInput parsed;

    if (auto r = assign(parsed.item, read_json_field<std::string>(input, "item", "todo_write")); !r)
    {
        return nonstd::make_unexpected(r.error());
    }

    if (auto r = assign(
            parsed.checked,
            read_json_field<bool, JsonFieldMode::optional_with_default>(
                input, "checked", "todo_write", false));
        !r)
    {
        return nonstd::make_unexpected(r.error());
    }

    if (auto r = assign(
            parsed.path,
            read_json_field<std::string, JsonFieldMode::optional_with_default>(
                input, "path", "todo_write", "TODO.md"));
        !r)
    {
        return nonstd::make_unexpected(r.error());
    }

    return parsed;
}

auto read_file_or_default(const std::filesystem::path& path) -> std::string
{
    if (!std::filesystem::exists(path))
    {
        return "# TODO\n\n";
    }

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return "# TODO\n\n";
    }

    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

} // namespace

auto TodoWriteTool::name() const -> std::string
{
    return "todo_write";
}

auto TodoWriteTool::description() const -> std::string
{
    return "Add a new TODO item or mark an existing one as done in a markdown checklist file. "
           "Creates the file if it doesn't exist.";
}

auto TodoWriteTool::permission_target(const ToolRequest& request) const -> PermissionTarget
{
    auto path = request.parsed_input.value<std::string>("path", "TODO.md");
    return PermissionTarget{
        .path = std::filesystem::path{path},
    };
}

auto TodoWriteTool::execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>
{
    auto parsed = parse_todo_write_input(request.parsed_input);
    if (!parsed)
    {
        return fail<ToolResponse>(parsed.error().kind, parsed.error().message);
    }

    auto resolved = resolve_workspace_path(context.cwd, parsed->path);
    if (!resolved)
    {
        return fail<ToolResponse>(resolved.error().kind, resolved.error().message);
    }

    auto existing = read_file_or_default(*resolved);

    // 先 trim item
    std::string trimmed_item_str(trim(parsed->item));

    auto unchecked_line = fmt::format("- [ ] {}", trimmed_item_str);
    auto checked_line = fmt::format("- [x] {}", trimmed_item_str);
    auto target_line = parsed->checked ? checked_line : unchecked_line;

    std::string updated;
    bool changed = false;

    if (existing.find(unchecked_line) != std::string::npos && parsed->checked)
    {
        // 将未勾选项标记为完成
        updated = existing;
        size_t pos = updated.find(unchecked_line);
        if (pos != std::string::npos)
        {
            updated.replace(pos, unchecked_line.length(), checked_line);
            changed = true;
        }
    }
    else if (existing.find(target_line) != std::string::npos)
    {
        // 项已处于目标状态，无需更改
        return ToolResponse{
            .tool_use_id = request.id,
            .content = fmt::format("No change needed in {}", resolved->string()),
            .is_error = false,
        };
    }
    else
    {
        // 新项，追加到文件
        auto trimmed = existing;
        // 移除尾部空白和换行，然后添加换行
        while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == ' ' || trimmed.back() == '\t'))
        {
            trimmed.pop_back();
        }
        updated = trimmed + "\n" + target_line + "\n";
        changed = true;
    }

    // 原子写入
    if (changed)
    {
        auto write_result = atomic_write_text_file(*resolved, updated);
        if (!write_result)
        {
            return fail<ToolResponse>(write_result.error().kind, write_result.error().message);
        }
    }

    return ToolResponse{
        .tool_use_id = request.id,
        .content = fmt::format("Updated {}", resolved->string()),
        .is_error = false,
    };
}

} // namespace codeharness
