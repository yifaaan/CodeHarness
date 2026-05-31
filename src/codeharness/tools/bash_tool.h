#pragma once

#include "codeharness/tools/tool.h"

namespace codeharness
{

// BashTool: 在子进程中执行 shell 命令并返回输出。
class BashTool final : public Tool
{
public:
    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;
};

} // namespace codeharness