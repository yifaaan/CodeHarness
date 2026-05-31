#include "codeharness/tools/read_file_tool.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
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

// 只返回文件中的一段“行范围”。
//
//   - content 是完整文件内容；
//   - offset 表示跳过前多少行，从 0 开始计数；
//   - limit 表示最多返回多少行。
auto slice_lines(std::string_view content, int offset, int limit) -> std::string
{
    std::string result;
    int current_line = 0;
    int selected_lines = 0;
    std::size_t line_start = 0;

    while (line_start < content.size() && selected_lines < limit)
    {
        // line_end 指向当前行的 '\n'；如果没有找到，说明当前行是最后一行。
        const auto newline = content.find('\n', line_start);
        const auto line_end = newline == std::string_view::npos ? content.size() : newline + 1;

        if (current_line >= offset)
        {
            // substr(line_start, count) 取出 [line_start, line_end) 这段原始内容，
            // 再 append 到结果里。line_end 可能包含 '\n'，所以输出会保留原来的换行。
            result.append(content.substr(line_start, line_end - line_start));
            ++selected_lines;
        }

        line_start = line_end;
        ++current_line;
    }

    return result;
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

auto ReadFileTool::is_read_only() const noexcept -> bool
{
    return true;
}

auto ReadFileTool::execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>
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

    // read_file 一次把文件读入内存，然后再按行截取。
    auto visible_content = slice_lines(*content, parsed_input->offset, parsed_input->limit);

    return ToolResponse{
        .tool_use_id = request.id,
        .content = std::move(visible_content),
        .is_error = false,
    };
}

} // namespace codeharness