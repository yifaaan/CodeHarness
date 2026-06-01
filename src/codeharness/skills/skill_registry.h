#pragma once

#include "codeharness/skills/skill.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace codeharness
{

class SkillRegistry
{
public:
    auto register_skill(SkillDefinition skill) -> void;
    auto get(std::string_view key) const -> const SkillDefinition*;
    auto list() const -> std::vector<SkillDefinition>;

private:
    std::unordered_map<std::string, std::shared_ptr<SkillDefinition>> by_key_;
};

} // namespace codeharness
