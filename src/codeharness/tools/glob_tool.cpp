#include "codeharness/tools/glob_tool.h"

#include <glob/glob.h>
#include <nlohmann/json.hpp>

#include <filesystem>

#include "codeharness/tools/workspace_path.h"

namespace codeharness
{

namespace
{

struct GlobInput
{
    std::string pattern;
    std::optional<std::string> path;
};

auto parse_glob_input(const nlohmann::json& input) -> Result<GlobInput>
{
    if (!input.contains("pattern") || !input["pattern"].is_string())
    {
        return fail<GlobInput>(ErrorKind::InvalidArgument, "glob requires string field: pattern");
    }

    GlobInput parsed;
    parsed.pattern = input["pattern"].get<std::string>();

    if (input.contains("path") && input["path"].is_string())
    {
        parsed.path = input["path"].get<std::string>();
    }

    return parsed;
}

constexpr int MAX_RESULTS = 200;

} // namespace

auto GlobTool::name() const -> std::string
{
    return "glob";
}

auto GlobTool::description() const -> std::string
{
    return "Search for files matching a glob pattern under the current workspace directory.";
}

auto GlobTool::is_read_only() const noexcept -> bool
{
    return true;
}

auto GlobTool::permission_target(const ToolRequest& request) const -> PermissionTarget
{
    return path_permission_target(request.input_json, "path");
}

auto GlobTool::execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>
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

    auto parsed_input = parse_glob_input(input);
    if (!parsed_input)
    {
        return fail<ToolResponse>(parsed_input.error().kind, parsed_input.error().message);
    }

    auto search_root = context.cwd;

    if (parsed_input->path)
    {
        auto resolved = resolve_workspace_path(context.cwd, *parsed_input->path);
        if (!resolved)
        {
            return fail<ToolResponse>(resolved.error().kind, resolved.error().message);
        }

        if (!std::filesystem::is_directory(*resolved))
        {
            return fail<ToolResponse>(ErrorKind::Io, "path is not a directory: " + resolved->string());
        }

        search_root = *resolved;
    }

    auto matches = glob::rglob(search_root.string() + '/' + parsed_input->pattern);

    nlohmann::json result_json = nlohmann::json::array();

    for (size_t i = 0; i < matches.size() && i < MAX_RESULTS; ++i)
    {
        std::error_code error;
        auto relative = std::filesystem::relative(matches[i], search_root, error);
        if (error)
        {
            continue;
        }

        result_json.push_back(relative.generic_string());
    }

    return ToolResponse{
        .tool_use_id = request.id,
        .content = result_json.dump(2),
        .is_error = false,
    };
}

} // namespace codeharness
