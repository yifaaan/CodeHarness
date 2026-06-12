#pragma once

#include "codeharness/tools/tool.h"

namespace codeharness {

class AskUserTool final : public Tool {
 public:
  auto name() const -> std::string override;
  auto description() const -> std::string override;
  auto is_read_only() const noexcept -> bool override;
  auto execute(const ToolRequest& request, const ToolContext& context) const -> absl::StatusOr<ToolResponse> override;
};

}  // namespace codeharness
