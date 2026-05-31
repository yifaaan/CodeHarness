#include "codeharness/tools/read_file_tool.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace codeharness
{

namespace
{

auto resolve_workspace_path(const std::filesystem::path& cwd, const std::filesystem::path& requested)
    -> Result<std::filesystem::path>
{
    if (requested.empty())
    {
        return fail<std::filesystem::path>(ErrorKind::InvalidArgument, "path is empty");
    }

    if (requested.is_absolute())
    {
        return fail<std::filesystem::path>(ErrorKind::InvalidArgument, "absolute paths are not allowed");
    }

    std::error_code error;
    // 规范化路径，去除其中的 .. 和 . 等
    auto base = std::filesystem::weakly_canonical(cwd, error);
    if (error)
    {
        return fail<std::filesystem::path>(ErrorKind::Io, "failed to resolve workspace path: " + error.message());
    }

    const auto target = std::filesystem::weakly_canonical(base / requested, error);
    if (error)
    {
        return fail<std::filesystem::path>(ErrorKind::Io, "failed to resolve path: " + error.message());
    }

    auto base_text = base.native();
    auto target_text = target.native();

    if (target_text.size() < base_text.size() || target_text.compare(0, base_text.size(), base_text) != 0)
    {
        return fail<std::filesystem::path>(ErrorKind::InvalidArgument, "path escapes cwd");
    }

    return target;
}

auto read_text_file(const std::filesystem::path& path) -> Result<std::string>
{
    std::ifstream file{path, std::ios::binary};
    if (!file)
    {
        return fail<std::string>(ErrorKind::Io, "failed to open file " + path.string());
    }

    std::ostringstream oss;
    oss << file.rdbuf();

    if (!file.good() && !file.eof())
    {
        return fail<std::string>(ErrorKind::Io, "failed to read file " + path.string());
    }

    return oss.str();
}

} // namespace

auto ReadFileTool::name() const -> std::string
{
    return "read_file";
}

auto ReadFileTool::description() const -> std::string
{
    return "Read a UTF-8 text file under the current workspace directory.";
}

auto ReadFileTool::execute(const ToolRequest& request, const ToolContext& context) -> Result<ToolResponse>
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

    if (!input.contains("path") || !input["path"].is_string())
    {
        return fail<ToolResponse>(ErrorKind::InvalidArgument, "read_file requires string field: path");
    }

    auto resolved_path = resolve_workspace_path(context.cwd, input["path"].get<std::string>());
    if (!resolved_path)
    {
        return fail<ToolResponse>(resolved_path.error().kind, resolved_path.error().message);
    }

    if (!std::filesystem::is_regular_file(*resolved_path))
    {
        return fail<ToolResponse>(ErrorKind::Io, "path is not a regular file: " + resolved_path->string());
    }

    auto content = read_text_file(*resolved_path);
    if (!content)
    {
        return fail<ToolResponse>(content.error().kind, content.error().message);
    }

    return ToolResponse{
        .tool_use_id = request.id,
        .content = std::move(*content),
        .is_error = false,
    };
}

} // namespace codeharness