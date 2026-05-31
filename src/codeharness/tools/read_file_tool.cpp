#include "codeharness/tools/read_file_tool.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

#include "codeharness/tools/workspace_path.h"

namespace codeharness
{

namespace
{

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

struct ReadFileInput
{
    std::filesystem::path path;
    int offset = 0;
    int limit = 200;
};

auto parse_read_file_input(const nlohmann::json& input) -> Result<ReadFileInput>
{
    if (!input.contains("path") || !input["path"].is_string())
    {
        return fail<ReadFileInput>(ErrorKind::InvalidArgument, "read_file requires string field: path");
    }

    if (input.contains("offset") && !input["offset"].is_number_integer())
    {
        return fail<ReadFileInput>(ErrorKind::InvalidArgument, "read_file offset must be an integer");
    }

    if (input.contains("limit") && !input["limit"].is_number_integer())
    {
        return fail<ReadFileInput>(ErrorKind::InvalidArgument, "read_file limit must be an integer");
    }

    ReadFileInput parsed;
    parsed.path = input["path"].get<std::string>();
    parsed.offset = input.value("offset", 0);
    parsed.limit = input.value("limit", 200);

    if (parsed.offset < 0 || parsed.limit <= 0)
    {
        return fail<ReadFileInput>(ErrorKind::InvalidArgument, "read_file offset/limit out of range");
    }

    return parsed;
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

    auto parsed_input = parse_read_file_input(input);
    if (!parsed_input)
    {
        return fail<ToolResponse>(parsed_input.error().kind, parsed_input.error().message);
    }

    auto resolved_path = resolve_workspace_path(context.cwd, parsed_input->path);
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