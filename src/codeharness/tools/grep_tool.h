#pragma once

#include "codeharness/tools/tool.h"

namespace codeharness
{

class GrepTool final : public Tool
{
public:
    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto is_read_only() const noexcept -> bool override;
    auto permission_target(const ToolRequest& request) const -> PermissionTarget override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;
};

} // namespace codeharness
