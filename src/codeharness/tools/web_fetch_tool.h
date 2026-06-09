#pragma once

#include "codeharness/tools/tool.h"

namespace codeharness
{

// WebFetchTool：抓取网页并返回文本。
//
//   - 参数: url (字符串), max_chars (整数，默认 12000，范围 500-50000)
//   - 支持 HTML 到文本转换
//   - 只读工具
class WebFetchTool final : public Tool
{
public:
    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto is_read_only() const noexcept -> bool override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;
};

} // namespace codeharness
