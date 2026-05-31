#pragma once

#include "codeharness/core/result.h"
#include "codeharness/tools/tool.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace codeharness
{

class ToolRegistry
{
public:
    auto add(std::unique_ptr<Tool> tool) -> Result<void>;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>;
    auto names() const -> std::vector<std::string>;

private:
    std::unordered_map<std::string, std::unique_ptr<Tool>> tools_;
};

} // namespace codeharness
