#include "codeharness/tools/skill_tool.h"

#include <nonstd/expected.hpp>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>

#include "codeharness/core/json_parse.h"

namespace codeharness
{

SkillTool::SkillTool(const SkillRegistry& registry) : registry_(registry)
{
}

auto SkillTool::name() const -> std::string
{
    return "skill";
}

auto SkillTool::description() const -> std::string
{
    return "Read a bundled, user, or project skill by name.";
}

auto SkillTool::is_read_only() const noexcept -> bool
{
    return true;
}

auto SkillTool::execute(const ToolRequest& request, const ToolContext&) const -> Result<ToolResponse>
{
    auto name = read_json_field<std::string>(request.parsed_input, "name", "skill");
    if (!name)
    {
        return nonstd::make_unexpected(name.error());
    }

    auto* skill = registry_.get(*name);
    if (skill == nullptr)
    {
        std::string lowercase;
        lowercase.reserve(name->size());
        std::ranges::transform(*name, std::back_inserter(lowercase), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        skill = registry_.get(lowercase);
    }

    if (skill == nullptr)
    {
        return ToolResponse{
            .tool_use_id = request.id,
            .content = "Skill not found: " + *name,
            .is_error = true,
        };
    }

    if (skill->disable_model_invocation)
    {
        const auto command_name = skill->command_name.value_or(skill->name);
        return ToolResponse{
            .tool_use_id = request.id,
            .content = "Skill " + command_name + " can only be invoked by the user as /" + command_name + '.',
            .is_error = true,
        };
    }

    return ToolResponse{
        .tool_use_id = request.id,
        .content = skill->content,
        .is_error = false,
    };
}

} // namespace codeharness
