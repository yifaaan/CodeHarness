#pragma once

#include "codeharness/core/result.h"
#include "codeharness/skills/skill.h"

#include <filesystem>
#include <string>
#include <vector>

namespace codeharness
{

struct PluginManifest
{
    std::string name;
    std::string version = "0.0.0";
    std::string description;
    bool enabled_by_default = true;
    std::filesystem::path skills_dir = "skills";
};

struct LoadedPlugin
{
    PluginManifest manifest;
    std::filesystem::path path;
    std::filesystem::path manifest_path;
    bool enabled = false;
    std::vector<SkillDefinition> skills;
};

struct PluginLoadOptions
{
    bool load_default_user_plugins = true;
    bool allow_project_plugins = false;
    std::vector<std::filesystem::path> user_plugin_roots;
    std::vector<std::filesystem::path> extra_plugin_roots;
    std::vector<std::filesystem::path> project_plugin_dirs = {
        ".codeharness/plugins",
        ".openharness/plugins",
        ".agents/plugins",
        ".claude/plugins",
    };
};

auto default_user_plugin_roots() -> std::vector<std::filesystem::path>;

auto load_plugin_manifest(const std::filesystem::path& manifest_path) -> Result<PluginManifest>;

auto load_plugins(const std::filesystem::path& cwd, PluginLoadOptions options = {})
    -> Result<std::vector<LoadedPlugin>>;

} // namespace codeharness
