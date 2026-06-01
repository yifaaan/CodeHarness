#pragma once

#include "codeharness/tools/tool.h"

namespace codeharness
{

// EditFileTool：在已有文件中执行简单字符串替换。
//
// 只支持 old_string -> new_string：
//   - 默认要求 old_string 在文件中恰好出现一次
//   - replace_all=true 时才替换所有匹配
//   - 路径必须仍然位于 cwd 内
class EditFileTool final : public Tool
{
public:
    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto permission_target(const ToolRequest& request) const -> PermissionTarget override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;
};

} // namespace codeharness
