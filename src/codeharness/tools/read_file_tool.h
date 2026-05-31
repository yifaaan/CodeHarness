#pragma once

#include "codeharness/tools/tool.h"

namespace codeharness
{

class ReadFileTool final : public Tool
{
public:
    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto execute(const ToolRequest& request, const ToolContext& context) -> Result<ToolResponse> override;
};

} // namespace codeharness