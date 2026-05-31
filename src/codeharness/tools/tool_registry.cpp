#include "codeharness/tools/tool_registry.h"

#include <algorithm>
#include <nonstd/expected.hpp>
#include <utility>

#include "codeharness/core/error.h"

namespace codeharness
{

auto ToolRegistry::add(std::unique_ptr<Tool> tool) -> Result<void>
{
    auto name = tool->name();
    tools_.emplace(std::move(name), std::move(tool));
    return {};
}

auto ToolRegistry::execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse>
{
    auto it = tools_.find(request.name);
    if (it == tools_.end())
    {
        return fail<ToolResponse>(ErrorKind::Tool, "tool not found: " + request.name);
    }

    return it->second->execute(request, context);
}

auto ToolRegistry::names() const -> std::vector<std::string>
{
    std::vector<std::string> result;
    result.reserve(tools_.size());

    for (auto& [name, _] : tools_)
    {
        result.push_back(name);
    }

    std::ranges::sort(result);
    return result;
}

} // namespace codeharness