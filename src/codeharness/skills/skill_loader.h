#pragma once

#include "codeharness/core/result.h"
#include "codeharness/skills/skill.h"
#include "codeharness/skills/skill_registry.h"

#include <filesystem>
#include <span>
#include <string_view>
#include <vector>


//  把 SKILL.md 文件加载并解析成 SkillDefinition,并汇总到
//  SkillRegistry。流程分三层:
//    1. 解析单个 markdown 文件(parse_frontmatter / parse_skill_markdown)
//    2. 从一个或多个目录里扫描 SKILL.md(load_skills_from_dirs)
//    3. 顶层组合器(load_skill_registry)按 "bundled → user → extra → project"
//       顺序合并,后注册者覆盖先注册者(以 key 冲突为准)。

namespace codeharness
{

struct SkillLoadOptions
{
    bool load_default_bundled_skills = true;
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
