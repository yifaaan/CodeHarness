#pragma once

#include "codeharness/core/result.h"
#include "codeharness/skills/skill.h"

#include <filesystem>
#include <vector>

namespace codeharness
{

// 返回随项目源码一起提供的基础 bundled skills。
//
// 这些 markdown 文件来自 OpenHarness 的 bundled/content/*.md。这里返回 Result
// 是刻意的: bundled 内容现在是资源文件,读取或解析失败属于启动时配置/I/O
// 问题,应该沿项目的 Result 通道上抛,而不是静默丢失默认能力。
auto default_bundled_skill_dir() -> std::filesystem::path;
auto load_bundled_skills_from_dir(const std::filesystem::path& content_dir) -> Result<std::vector<SkillDefinition>>;
auto default_bundled_skills() -> Result<std::vector<SkillDefinition>>;

} // namespace codeharness
