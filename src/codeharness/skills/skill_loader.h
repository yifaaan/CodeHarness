#pragma once

#include "codeharness/core/result.h"
#include "codeharness/skills/skill.h"
#include "codeharness/skills/skill_registry.h"

#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace codeharness
{

struct SkillLoadOptions
{
    std::vector<SkillDefinition> bundled_skills;
    std::vector<std::filesystem::path> user_skill_dirs;
    std::vector<std::filesystem::path> extra_skill_dirs;
    std::vector<std::filesystem::path> project_skill_dirs = {
        ".codeharness/skills",
        ".agents/skills",
        ".claude/skills",
    };
    bool load_default_user_skills = true;
    bool allow_project_skills = true;
};

auto default_user_skill_dirs() -> std::vector<std::filesystem::path>;

auto parse_skill_markdown(std::string_view default_name, std::string content, std::string source = {})
    -> SkillDefinition;

auto load_skill_file(const std::filesystem::path& path, std::string source) -> Result<SkillDefinition>;

auto load_skills_from_dirs(std::span<const std::filesystem::path> directories, std::string_view source)
    -> Result<std::vector<SkillDefinition>>;

auto discover_project_skill_dirs(const std::filesystem::path& cwd,
                                 std::span<const std::filesystem::path> relative_dirs)
    -> Result<std::vector<std::filesystem::path>>;

auto load_skill_registry(const std::filesystem::path& cwd, SkillLoadOptions options = {}) -> Result<SkillRegistry>;

} // namespace codeharness
