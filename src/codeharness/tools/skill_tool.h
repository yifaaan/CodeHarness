#pragma once

#include "codeharness/skills/skill_registry.h"
#include "codeharness/tools/tool.h"

namespace codeharness
{

class SkillTool final : public Tool
{
public:
    explicit SkillTool(const SkillRegistry& registry);

    auto name() const -> std::string override;
    auto description() const -> std::string override;

    auto is_read_only() const noexcept -> bool override;
    auto execute(const ToolRequest& request, const ToolContext& context) const -> Result<ToolResponse> override;

private:
    const SkillRegistry& registry_;
};

} // namespace codeharness
