#pragma once

#include "codeharness/tools/tool.h"

namespace codeharness {

// TodoWriteTool：在 TODO 文件中添加或更新 TODO 项。
//
//   - 参数: item (文本), checked (是否完成), path (文件路径，默认 TODO.md)
//   - 自动检查 cwd 下路径安全
//   - 未勾选项标记为完成：- [ ] -> - [x]
//   - 新项追加到文件末尾
class TodoWriteTool final : public Tool {
 public:
  auto name() const -> std::string override;
  auto description() const -> std::string override;
  auto permission_target(const ToolRequest& request) const -> PermissionTarget override;
  auto execute(const ToolRequest& request, const ToolContext& context) const -> absl::StatusOr<ToolResponse> override;
};

}  // namespace codeharness
