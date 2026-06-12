#include "codeharness/tools/read_file_tool.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string_view>

#include "codeharness/core/assign.h"
#include "codeharness/core/error.h"
#include "codeharness/core/json_parse.h"
#include "codeharness/tools/text_file.h"
#include "codeharness/tools/workspace_path.h"

namespace codeharness {

namespace {

struct ReadFileInput {
  std::filesystem::path path;
  int offset = 0;
  int limit = 200;
};

auto parse_read_file_input(const nlohmann::json& input) -> absl::StatusOr<ReadFileInput> {
  ReadFileInput parsed;

  if (auto r = Assign(parsed.path, ReadJsonField<std::string>(input, "path", "read_file")); !r.ok()) {
    return r.status();
  }

  if (auto r = Assign(parsed.offset,
                      ReadJsonField<int, JsonFieldMode::kOptionalWithDefault>(input, "offset", "read_file", 0));
      !r.ok()) {
    return r.status();
  }

  if (auto r = Assign(parsed.limit,
                      ReadJsonField<int, JsonFieldMode::kOptionalWithDefault>(input, "limit", "read_file", 200));
      !r.ok()) {
    return r.status();
  }

  if (parsed.offset < 0 || parsed.limit <= 0) {
    return absl::InvalidArgumentError("read_file offset/limit out of range");
  }

  return parsed;
}

// 只返回文件中的一段"行范围"。
//
//   - content 是完整文件内容；
//   - offset 表示跳过前多少行，从 0 开始计数；
//   - limit 表示最多返回多少行。
auto slice_lines(std::string_view content, int offset, int limit) -> std::string {
  std::string result;
  int current_line = 0;
  int selected_lines = 0;
  std::size_t line_start = 0;

  while (line_start < content.size() && selected_lines < limit) {
    // line_end 指向当前行的 '\n'；如果没有找到，说明当前行是最后一行。
    const auto newline = content.find('\n', line_start);
    const auto line_end = newline == std::string_view::npos ? content.size() : newline + 1;

    if (current_line >= offset) {
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

}  // namespace

auto ReadFileTool::name() const -> std::string { return "read_file"; }

auto ReadFileTool::description() const -> std::string {
  return "Read a UTF-8 text file under the current workspace directory.";
}

auto ReadFileTool::is_read_only() const noexcept -> bool { return true; }

auto ReadFileTool::permission_target(const ToolRequest& request) const -> PermissionTarget {
  return path_permission_target(request.parsed_input, "path");
}

auto ReadFileTool::execute(const ToolRequest& request, const ToolContext& context) const
    -> absl::StatusOr<ToolResponse> {
  auto parsed_input = parse_read_file_input(request.parsed_input);
  if (!parsed_input.ok()) {
    return parsed_input.status();
  }

  auto resolved_path = resolve_workspace_path(context.cwd, parsed_input->path);
  if (!resolved_path.ok()) {
    return resolved_path.status();
  }

  if (!std::filesystem::is_regular_file(*resolved_path)) {
    return absl::InternalError("path is not a regular file: " + resolved_path->string());
  }

  auto content = read_text_file(*resolved_path);
  if (!content.ok()) {
    return content.status();
  }

  // read_file 一次把文件读入内存，然后再按行截取。
  auto visible_content = slice_lines(*content, parsed_input->offset, parsed_input->limit);

  return ToolResponse{
      .tool_use_id = request.id,
      .content = std::move(visible_content),
      .is_error = false,
  };
}

}  // namespace codeharness
