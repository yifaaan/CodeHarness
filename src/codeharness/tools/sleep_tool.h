#pragma once

#include "codeharness/tools/tool.h"

namespace codeharness {

// SleepTool：延迟执行（主要用于测试）。
//
//   - 参数: seconds (浮点数，默认 1.0)
//   - 只读工具，无副作用
//   - 使用 std::this_thread::sleep_for
class SleepTool final : public Tool {
 public:
  auto name() const -> std::string override;
  auto description() const -> std::string override;
  auto is_read_only() const noexcept -> bool override;
  auto execute(const ToolRequest& request, const ToolContext& context) const -> absl::StatusOr<ToolResponse> override;
};

}  // namespace codeharness
