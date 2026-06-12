#include "codeharness/tools/write_file_tool.h"

#include <fmt/format.h>

#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "codeharness/core/assign.h"
#include "codeharness/core/error.h"
#include "codeharness/core/json_parse.h"
#include "codeharness/tools/text_file.h"
#include "codeharness/tools/workspace_path.h"

namespace codeharness {

namespace {

// WriteFileTool 的输入参数结构体
struct WriteFileInput {
  std::filesystem::path path;      // 要写入的文件路径（相对于 cwd）
  std::string content;             // 要写入的文本内容
  bool create_directories = true;  // 父目录不存在时是否自动创建
};

// 从 JSON 中解析 WriteFileInput
// 必须字段：path (string), content (string)
// 可选字段：create_directories (bool, 默认 true)
auto parse_write_file_input(const nlohmann::json& input) -> absl::StatusOr<WriteFileInput> {
  WriteFileInput parsed;

  if (auto r = Assign(parsed.path, ReadJsonField<std::string>(input, "path", "write_file")); !r.ok()) {
    return r.status();
  }

  if (auto r = Assign(parsed.content, ReadJsonField<std::string>(input, "content", "write_file")); !r.ok()) {
    return r.status();
  }

  if (auto r = Assign(parsed.create_directories, ReadJsonField<bool, JsonFieldMode::kOptionalWithDefault>(
                                                     input, "create_directories", "write_file", true));
      !r.ok()) {
    return r.status();
  }

  return parsed;
}

auto format_size(std::uint64_t bytes) -> std::string {
  if (bytes < 1024) {
    return fmt::format("{} B", bytes);
  } else if (bytes < 1024 * 1024) {
    return fmt::format("{} KB", bytes / 1024);
  } else if (bytes < 1024 * 1024 * 1024) {
    return fmt::format("{} MB", bytes / (1024 * 1024));
  } else {
    return fmt::format("{} GB", bytes / (1024 * 1024 * 1024));
  }
}

}  // namespace

auto WriteFileTool::name() const -> std::string { return "write_file"; }

auto WriteFileTool::description() const -> std::string {
  return "Write content to a file under the current workspace directory. "
         "Uses atomic write (write to temp file then rename) to prevent corruption.";
}

auto WriteFileTool::permission_target(const ToolRequest& request) const -> PermissionTarget {
  return path_permission_target(request.parsed_input, "path");
}

auto WriteFileTool::execute(const ToolRequest& request, const ToolContext& context) const
    -> absl::StatusOr<ToolResponse> {
  auto parsed = parse_write_file_input(request.parsed_input);
  if (!parsed.ok()) {
    return parsed.status();
  }

  auto resolved = resolve_workspace_path(context.cwd, parsed->path);
  if (!resolved.ok()) {
    return resolved.status();
  }

  // 自动创建父目录
  auto parent = resolved->parent_path();
  if (!parent.empty() && !std::filesystem::exists(parent)) {
    if (!parsed->create_directories) {
      return absl::InternalError("parent directory does not exist: " + parent.string());
    }

    std::error_code dir_error;
    std::filesystem::create_directories(parent, dir_error);
    if (dir_error) {
      return absl::InternalError("failed to create parent directory: " + dir_error.message());
    }
  }

  // 是否覆盖已有文件
  bool is_overwrite = std::filesystem::exists(*resolved);

  // 原子写入
  auto write_result = atomic_write_text_file(*resolved, parsed->content);
  if (!write_result.ok()) {
    return write_result.status();
  }

  auto size_str = format_size(parsed->content.size());

  std::string result_text;
  if (is_overwrite) {
    result_text = fmt::format("Overwrote {} ({})", resolved->string(), size_str);
  } else {
    result_text = fmt::format("Created {} ({})", resolved->string(), size_str);
  }

  return ToolResponse{
      .tool_use_id = request.id,
      .content = result_text,
      .is_error = false,
  };
}

}  // namespace codeharness
