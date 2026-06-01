#include "codeharness/tools/edit_file_tool.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <format>
#include <string>
#include <utility>

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

    auto path = require_string(input, "path", "edit_file");
    if (!path)
    {
        return fail<EditFileInput>(path.error().kind, path.error().message);
    }
    parsed.path = std::move(*path);

    auto old_string = require_string(input, "old_string", "edit_file");
    if (!old_string)
    {
        return fail<EditFileInput>(old_string.error().kind, old_string.error().message);
    }
    parsed.old_string = std::move(*old_string);

    auto new_string = require_string(input, "new_string", "edit_file");
    if (!new_string)
    {
        return fail<EditFileInput>(new_string.error().kind, new_string.error().message);
    }
    parsed.new_string = std::move(*new_string);

    auto replace_all = optional_bool(input, "replace_all", false, "edit_file");
    if (!replace_all)
    {
        return fail<EditFileInput>(replace_all.error().kind, replace_all.error().message);
    }
    parsed.replace_all = *replace_all;

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

auto replace_one(std::string content, const std::string& old_text, const std::string& new_text) -> std::string
{
    const auto position = content.find(old_text);
    content.replace(position, old_text.size(), new_text);
    return content;
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
    return path_permission_target(request.input_json, "path");
}

auto EditFileTool::execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>
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

    auto parsed = parse_edit_file_input(input);
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
                      : replace_one(std::move(*content), parsed->old_string, parsed->new_string);

    auto write_result = atomic_write_text_file(*resolved, edited);
    if (!write_result)
    {
        return fail<ToolResponse>(write_result.error().kind, write_result.error().message);
    }

    auto unit = match_count == 1 ? "replacement" : "replacements";

    return ToolResponse{
        .tool_use_id = request.id,
        .content = std::format("Edited {} ({} {})", resolved->string(), match_count, unit),
        .is_error = false,
    };
}

} // namespace codeharness
