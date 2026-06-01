#pragma once

#include "codeharness/tools/tool.h"

namespace codeharness
{

// WriteFileTool：将文本内容写入指定文件。
//
//   - 路径必须在 cwd 之下
//   - 使用 atomic write 模式：先写入 .tmp 临时文件，再 rename 到目标路径
//   - 父目录不存在时自动创建
class WriteFileTool final : public Tool
{
public:
    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto permission_target(const ToolRequest& request) const -> PermissionTarget override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;
};

} // namespace codeharness
