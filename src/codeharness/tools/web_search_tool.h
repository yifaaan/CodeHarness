#pragma once

#include "codeharness/tools/tool.h"

namespace codeharness
{

// WebSearchTool：执行网页搜索并返回结果列表。
//
//   - 参数: query (字符串), max_results (整数，默认 5，范围 1-10)
//   - 默认使用 DuckDuckGo HTML 搜索
//   - 可通过环境变量或参数指定搜索后端
//   - 只读工具
class WebSearchTool final : public Tool
{
public:
    auto name() const -> std::string override;
    auto description() const -> std::string override;
    auto is_read_only() const noexcept -> bool override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;
};

} // namespace codeharness
