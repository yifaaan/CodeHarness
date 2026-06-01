#include "codeharness/skills/skill_registry.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace codeharness
{

auto SkillRegistry::register_skill(SkillDefinition skill) -> void
{
    auto stored = std::make_shared<SkillDefinition>(std::move(skill));

    const auto register_key = [&](const std::string& key) {
        if (!key.empty())
        {
            by_key_[key] = stored;
        }
    };

    register_key(stored->name);

    if (stored->command_name)
    {
        register_key(*stored->command_name);
    }

    if (stored->display_name)
    {
        register_key(*stored->display_name);
    }

    for (const auto& alias : stored->aliases)
    {
        register_key(alias);
    }
}

auto SkillRegistry::get(std::string_view key) const -> const SkillDefinition*
{
    const auto it = by_key_.find(std::string{key});
    if (it == by_key_.end())
    {
        return nullptr;
    }

    return it->second.get();
}

auto SkillRegistry::list() const -> std::vector<SkillDefinition>
{
    std::vector<SkillDefinition> skills;
    std::unordered_set<const SkillDefinition*> seen;

    for (const auto& [_, skill] : by_key_)
    {
        if (seen.emplace(skill.get()).second)
        {
            skills.push_back(*skill);
        }
    }

    std::ranges::sort(skills, [](const auto& left, const auto& right) {
        return left.command_name.value_or(left.name) < right.command_name.value_or(right.name);
    });

    return skills;
}

} // namespace codeharness
