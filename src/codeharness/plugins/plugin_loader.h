#pragma once

#include "codeharness/core/error.h"
#include "codeharness/hooks/hook.h"
#include "codeharness/mcp/types.h"
#include "codeharness/skills/skill.h"

#include <filesystem>
#include <optional>
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
    std::filesystem::path commands_dir = "commands";
    std::filesystem::path mcp_file = "mcp.json";
    std::filesystem::path hooks_file = "hooks.json";
};

struct PluginCommandDefinition
{
    std::string name;
    std::string command_name;
    std::string description;
    std::string content;
    std::string source_plugin;
    std::filesystem::path path;
    bool disable_model_invocation = false;
    std::optional<std::string> model;
};

struct LoadedPlugin
{
    PluginManifest manifest;
    std::filesystem::path path;
    std::filesystem::path manifest_path;
    bool enabled = false;
    std::vector<SkillDefinition> skills;
    std::vector<PluginCommandDefinition> commands;
    std::vector<McpServerConfig> mcp_servers;
    std::vector<HookDefinition> hooks;
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

auto load_plugin_manifest(const std::filesystem::path& manifest_path) -> absl::StatusOr<PluginManifest>;

auto load_plugins(const std::filesystem::path& cwd, PluginLoadOptions options = {})
    -> absl::StatusOr<std::vector<LoadedPlugin>>;

} // namespace codeharness
