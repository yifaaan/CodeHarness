#include "codeharness/tools/edit_file_tool.h"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <nonstd/expected.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>

#include "codeharness/core/assign.h"
#include "codeharness/core/json_parse.h"
#include "codeharness/tools/text_file.h"
#include "codeharness/tools/workspace_path.h"

namespace codeharness
{

namespace
{

struct EditFileInput
{
    std::filesystem::path path;
    std::string old_string;
    std::string new_string;
    bool replace_all = false;
};

auto parse_edit_file_input(const nlohmann::json& input) -> Result<EditFileInput>
{
    EditFileInput parsed;

    if (auto r = assign(parsed.path, read_json_field<std::string>(input, "path", "edit_file")); !r)
    {
        return nonstd::make_unexpected(r.error());
    }

    if (auto r = assign(parsed.old_string, read_json_field<std::string>(input, "old_string", "edit_file")); !r)
    {
        return nonstd::make_unexpected(r.error());
    }

    if (auto r = assign(parsed.new_string, read_json_field<std::string>(input, "new_string", "edit_file")); !r)
    {
        return nonstd::make_unexpected(r.error());
    }

    if (auto r = assign(
            parsed.replace_all,
            read_json_field<bool, JsonFieldMode::optional_with_default>(input, "replace_all", "edit_file", false));
        !r)
    {
        return nonstd::make_unexpected(r.error());
    }

    if (parsed.old_string.empty())
    {
        return fail<EditFileInput>(ErrorKind::InvalidArgument, "edit_file old_string must not be empty");
    }

    return parsed;
}

auto count_occurrences(const std::string& content, const std::string& needle) -> std::size_t
{
    std::size_t count = 0;
    std::size_t position = 0;

    while ((position = content.find(needle, position)) != std::string::npos)
    {
        ++count;
        position += needle.size();
    }

    return count;
}

auto replace_all_occurrences(std::string content, const std::string& old_text, const std::string& new_text)
    -> std::string
{
    std::size_t position = 0;

    while ((position = content.find(old_text, position)) != std::string::npos)
    {
        content.replace(position, old_text.size(), new_text);
        position += new_text.size();
    }

    return content;
}

} // namespace

auto EditFileTool::name() const -> std::string
{
    return "edit_file";
}

auto EditFileTool::description() const -> std::string
{
    return "Edit an existing text file by replacing old_string with new_string under the current workspace directory.";
}

auto EditFileTool::permission_target(const ToolRequest& request) const -> PermissionTarget
{
    return path_permission_target(request.parsed_input, "path");
}

auto EditFileTool::execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>
{
    auto parsed = parse_edit_file_input(request.parsed_input);
    if (!parsed)
    {
        return fail<ToolResponse>(parsed.error().kind, parsed.error().message);
    }

    auto resolved = resolve_workspace_path(context.cwd, parsed->path);
    if (!resolved)
    {
        return fail<ToolResponse>(resolved.error().kind, resolved.error().message);
    }

    if (!std::filesystem::is_regular_file(*resolved))
    {
        return fail<ToolResponse>(ErrorKind::Io, "path is not a regular file: " + resolved->string());
    }

    auto content = read_text_file(*resolved);
    if (!content)
    {
        return fail<ToolResponse>(content.error().kind, content.error().message);
    }

    auto match_count = count_occurrences(*content, parsed->old_string);
    if (match_count == 0)
    {
        return fail<ToolResponse>(ErrorKind::InvalidArgument, "edit_file old_string was not found");
    }

    if (match_count > 1 && !parsed->replace_all)
    {
        return fail<ToolResponse>(
            ErrorKind::InvalidArgument,
            "edit_file old_string matched multiple locations; set replace_all=true to replace all matches");
    }

    auto edited = parsed->replace_all
                      ? replace_all_occurrences(std::move(*content), parsed->old_string, parsed->new_string)
                      : [&] {
                            auto text = std::move(*content);
                            const auto position = text.find(parsed->old_string);
                            text.replace(position, parsed->old_string.size(), parsed->new_string);
                            return text;
                        }();

    auto write_result = atomic_write_text_file(*resolved, edited);
    if (!write_result)
    {
        return fail<ToolResponse>(write_result.error().kind, write_result.error().message);
    }

    auto unit = match_count == 1 ? "replacement" : "replacements";

    return ToolResponse{
        .tool_use_id = request.id,
        .content = fmt::format("Edited {} ({} {})", resolved->string(), match_count, unit),
        .is_error = false,
    };
}

} // namespace codeharness
