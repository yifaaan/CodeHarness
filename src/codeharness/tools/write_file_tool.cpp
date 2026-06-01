#include "codeharness/tools/write_file_tool.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <format>

#include "codeharness/tools/text_file.h"
#include "codeharness/tools/workspace_path.h"

namespace codeharness
{

namespace
{

// WriteFileTool 的输入参数结构体
struct WriteFileInput
{
    std::filesystem::path path;     // 要写入的文件路径（相对于 cwd）
    std::string content;            // 要写入的文本内容
    bool create_directories = true; // 父目录不存在时是否自动创建
};

// 从 JSON 中解析 WriteFileInput
// 必须字段：path (string), content (string)
// 可选字段：create_directories (bool, 默认 true)
auto parse_write_file_input(const nlohmann::json& input) -> Result<WriteFileInput>
{
    if (!input.contains("path") || !input["path"].is_string())
    {
        return fail<WriteFileInput>(ErrorKind::InvalidArgument, "write_file requires string field: path");
    }

    if (!input.contains("content") || !input["content"].is_string())
    {
        return fail<WriteFileInput>(ErrorKind::InvalidArgument, "write_file requires string field: content");
    }

    if (input.contains("create_directories") && !input["create_directories"].is_boolean())
    {
        return fail<WriteFileInput>(ErrorKind::InvalidArgument, "write_file create_directories must be a boolean");
    }

    WriteFileInput parsed;
    parsed.path = input["path"].get<std::string>();
    parsed.content = input["content"].get<std::string>();
    parsed.create_directories = input.value("create_directories", true);

    return parsed;
}

auto format_size(std::uint64_t bytes) -> std::string
{
    if (bytes < 1024)
    {
        return std::format("{} B", bytes);
    }
    else if (bytes < 1024 * 1024)
    {
        return std::format("{} KB", bytes / 1024);
    }
    else if (bytes < 1024 * 1024 * 1024)
    {
        return std::format("{} MB", bytes / (1024 * 1024));
    }
    else
    {
        return std::format("{} GB", bytes / (1024 * 1024 * 1024));
    }
}

} // namespace

auto WriteFileTool::name() const -> std::string
{
    return "write_file";
}

auto WriteFileTool::description() const -> std::string
{
    return "Write content to a file under the current workspace directory. "
           "Uses atomic write (write to temp file then rename) to prevent corruption.";
}

auto WriteFileTool::permission_target(const ToolRequest& request) const -> PermissionTarget
{
    return path_permission_target(request.input_json, "path");
}

auto WriteFileTool::execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>
{
    nlohmann::json input;
    try
    {
        input = nlohmann::json::parse(request.input_json);
    }
    catch (const nlohmann::json::parse_error& e)
    {
        return fail<ToolResponse>(ErrorKind::InvalidArgument, e.what());
    }

    auto parsed = parse_write_file_input(input);
    if (!parsed)
    {
        return fail<ToolResponse>(parsed.error().kind, parsed.error().message);
    }

    auto resolved = resolve_workspace_path(context.cwd, parsed->path);
    if (!resolved)
    {
        return fail<ToolResponse>(resolved.error().kind, resolved.error().message);
    }

    // 自动创建父目录
    auto parent = resolved->parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent))
    {
        if (!parsed->create_directories)
        {
            return fail<ToolResponse>(ErrorKind::Io, "parent directory does not exist: " + parent.string());
        }

        std::error_code dir_error;
        std::filesystem::create_directories(parent, dir_error);
        if (dir_error)
        {
            return fail<ToolResponse>(ErrorKind::Io, "failed to create parent directory: " + dir_error.message());
        }
    }

    // 是否覆盖已有文件
    bool is_overwrite = std::filesystem::exists(*resolved);

    // 原子写入
    auto write_result = atomic_write_text_file(*resolved, parsed->content);
    if (!write_result)
    {
        return fail<ToolResponse>(write_result.error().kind, write_result.error().message);
    }

    auto size_str = format_size(parsed->content.size());

    std::string result_text;
    if (is_overwrite)
    {
        result_text = std::format("Overwrote {} ({})", resolved->string(), size_str);
    }
    else
    {
        result_text = std::format("Created {} ({})", resolved->string(), size_str);
    }

    return ToolResponse{
        .tool_use_id = request.id,
        .content = result_text,
        .is_error = false,
    };
}

} // namespace codeharness
