#include "codeharness/plugins/plugin_loader.h"

#include <glob/glob.h>
#include <nlohmann/json.hpp>
#include <nonstd/expected.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "codeharness/core/json_parse.h"
#include "codeharness/core/paths.h"
#include "codeharness/skills/skill_loader.h"
#include "codeharness/tools/text_file.h"

namespace codeharness
{

namespace
{

// Try parsing a field from JSON using snake_case first, then camelCase as fallback.
// This accommodates both naming conventions in plugin manifests.
template <typename T>
auto read_aliased_field(const nlohmann::json& json, std::string_view snake_key, std::string_view camel_key, std::string_view context, T& target) -> Result<void>
{
    for (auto key : {snake_key, camel_key})
    {
        if (!json.contains(std::string{key}))
        {
            continue;
        }

        auto value = read_json_field<T>(json, key, context);
        if (!value)
        {
            return nonstd::make_unexpected(value.error());
        }

        target = std::move(*value);
        return {};
    }

    return {};
}

auto parse_manifest_json(const nlohmann::json& json, const std::filesystem::path& manifest_path)
    -> Result<PluginManifest>
{
    if (!json.is_object())
    {
        return fail<PluginManifest>(
            ErrorKind::InvalidArgument, "plugin manifest must be a JSON object: " + manifest_path.string());
    }

    auto name = read_json_field<std::string>(json, "name", "plugin manifest");
    if (!name)
    {
        return nonstd::make_unexpected(name.error());
    }

    auto manifest = PluginManifest{.name = std::move(*name)};

    if (auto r = read_aliased_field(json, "version", "version", "plugin manifest", manifest.version); !r) { return nonstd::make_unexpected(r.error()); }
    if (auto r = read_aliased_field(json, "description", "description", "plugin manifest", manifest.description); !r) { return nonstd::make_unexpected(r.error()); }
    if (auto r = read_aliased_field(json, "enabled_by_default", "enabledByDefault", "plugin manifest", manifest.enabled_by_default); !r) { return nonstd::make_unexpected(r.error()); }

    // Path fields: read as string then convert to std::filesystem::path.
    auto read_path_field = [&](const char* snake, const char* camel, std::filesystem::path& target) -> Result<void> {
        for (auto key : {snake, camel})
        {
            if (!json.contains(key)) { continue; }
            auto value = read_json_field<std::string>(json, key, "plugin manifest");
            if (!value) { return nonstd::make_unexpected(value.error()); }
            target = std::filesystem::path{std::move(*value)};
            return {};
        }
        return {};
    };
    if (auto r = read_path_field("skills_dir", "skillsDir", manifest.skills_dir); !r) { return nonstd::make_unexpected(r.error()); }
    if (auto r = read_path_field("commands_dir", "commandsDir", manifest.commands_dir); !r) { return nonstd::make_unexpected(r.error()); }
    if (auto r = read_path_field("mcp_file", "mcpFile", manifest.mcp_file); !r) { return nonstd::make_unexpected(r.error()); }
    if (auto r = read_path_field("hooks_file", "hooksFile", manifest.hooks_file); !r) { return nonstd::make_unexpected(r.error()); }

    if (!is_safe_relative_path(manifest.skills_dir))
    {
        return fail<PluginManifest>(
            ErrorKind::InvalidArgument, "plugin skills_dir must be a safe relative path: " + manifest_path.string());
    }

    if (!is_safe_relative_path(manifest.commands_dir))
    {
        return fail<PluginManifest>(
            ErrorKind::InvalidArgument, "plugin commands_dir must be a safe relative path: " + manifest_path.string());
    }

    if (!is_safe_relative_path(manifest.mcp_file))
    {
        return fail<PluginManifest>(
            ErrorKind::InvalidArgument, "plugin mcp_file must be a safe relative path: " + manifest_path.string());
    }

    if (!is_safe_relative_path(manifest.hooks_file))
    {
        return fail<PluginManifest>(
            ErrorKind::InvalidArgument, "plugin hooks_file must be a safe relative path: " + manifest_path.string());
    }

    return manifest;
}

auto discover_manifest_paths(std::span<const std::filesystem::path> roots) -> Result<std::vector<std::filesystem::path>>
{
    std::vector<std::filesystem::path> manifests;
    std::set<std::filesystem::path> seen;

    for (const auto& root : roots)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(root, error))
        {
            continue;
        }

        std::vector<std::filesystem::path> candidates;
        try
        {
            auto direct = glob::glob(root.string() + "/*/plugin.json");
            auto claude = glob::glob(root.string() + "/*/.claude-plugin/plugin.json");
            candidates.insert(candidates.end(), direct.begin(), direct.end());
            candidates.insert(candidates.end(), claude.begin(), claude.end());
        }
        catch (const std::exception& e)
        {
            return fail<std::vector<std::filesystem::path>>(
                ErrorKind::Io, fmt::format("failed to scan plugin root {}: {}", root.string(), e.what()));
        }

        std::ranges::sort(candidates);

        for (const auto& candidate : candidates)
        {
            const auto canonical = std::filesystem::weakly_canonical(candidate, error);
            if (error)
            {
                return fail<std::vector<std::filesystem::path>>(
                    ErrorKind::Io,
                    fmt::format("failed to resolve plugin manifest {}: {}", candidate.string(), error.message()));
            }

            if (seen.emplace(canonical).second)
            {
                manifests.push_back(canonical);
            }
        }
    }

    return manifests;
}

auto plugin_base_dir(const std::filesystem::path& manifest_path) -> std::filesystem::path
{
    if (manifest_path.parent_path().filename() == ".claude-plugin")
    {
        return manifest_path.parent_path().parent_path();
    }

    return manifest_path.parent_path();
}

auto source_for_plugin(const PluginManifest& manifest) -> std::string
{
    return "plugin:" + manifest.name;
}

auto command_name_for_plugin(const PluginManifest& manifest, std::string_view command_name) -> std::string
{
    return manifest.name + ":" + std::string{command_name};
}

auto load_plugin_command_file(const std::filesystem::path& path, const PluginManifest& manifest)
    -> Result<PluginCommandDefinition>
{
    auto content = read_text_file(path);
    if (!content)
    {
        return nonstd::make_unexpected(content.error());
    }

    const auto name = path.stem().string();
    auto parsed = parse_skill_markdown(name, std::move(*content), source_for_plugin(manifest));

    return PluginCommandDefinition{
        .name = name,
        .command_name = command_name_for_plugin(manifest, name),
        .description = std::move(parsed.description),
        .content = std::move(parsed.content),
        .source_plugin = manifest.name,
        .path = path,
        .disable_model_invocation = parsed.disable_model_invocation,
        .model = std::move(parsed.model),
    };
}

auto load_plugin_commands(const std::filesystem::path& root, const PluginManifest& manifest)
    -> Result<std::vector<PluginCommandDefinition>>
{
    std::error_code error;
    if (!std::filesystem::is_directory(root, error))
    {
        return std::vector<PluginCommandDefinition>{};
    }

    std::vector<std::filesystem::path> matches;
    try
    {
        matches = glob::glob(root.string() + "/*.md");
    }
    catch (const std::exception& e)
    {
        return fail<std::vector<PluginCommandDefinition>>(
            ErrorKind::Io, fmt::format("failed to scan plugin command directory {}: {}", root.string(), e.what()));
    }

    std::ranges::sort(matches);

    std::vector<PluginCommandDefinition> commands;
    commands.reserve(matches.size());
    for (const auto& candidate : matches)
    {
        auto command = load_plugin_command_file(candidate, manifest);
        if (!command)
        {
            return nonstd::make_unexpected(command.error());
        }

        commands.push_back(std::move(*command));
    }

    return commands;
}

auto read_json_file(const std::filesystem::path& path, std::string_view label) -> Result<nlohmann::json>
{
    auto content = read_text_file(path);
    if (!content)
    {
        return nonstd::make_unexpected(content.error());
    }

    try
    {
        return nlohmann::json::parse(*content);
    }
    catch (const nlohmann::json::parse_error& error)
    {
        return fail<nlohmann::json>(
            ErrorKind::InvalidArgument, fmt::format("failed to parse {} {}: {}", label, path.string(), error.what()));
    }
}

auto parse_stdio_mcp_server(std::string name, const nlohmann::json& json, std::string_view context)
    -> Result<McpServerConfig>
{
    auto command = read_json_field<std::string>(json, "command", context);
    if (!command)
    {
        return nonstd::make_unexpected(command.error());
    }

    auto args =
        read_json_field<std::vector<std::string>, JsonFieldMode::optional_with_default>(json, "args", context, {});
    if (!args)
    {
        return nonstd::make_unexpected(args.error());
    }

    auto env = read_json_field<std::map<std::string, std::string>, JsonFieldMode::optional_with_default>(
        json, "env", context, {});
    if (!env)
    {
        return nonstd::make_unexpected(env.error());
    }

    auto cwd = read_optional_json_field<std::string>(json, "cwd", context);
    if (!cwd)
    {
        return nonstd::make_unexpected(cwd.error());
    }

    auto config = McpStdioServerConfig{
        .name = std::move(name),
        .command = std::move(*command),
        .args = std::move(*args),
        .env = std::move(*env),
    };
    if (*cwd)
    {
        config.cwd = std::filesystem::path{**cwd};
    }

    return McpServerConfig{std::move(config)};
}

auto parse_http_mcp_server(std::string name, const nlohmann::json& json, std::string_view context)
    -> Result<McpServerConfig>
{
    auto url = read_json_field<std::string>(json, "url", context);
    if (!url)
    {
        return nonstd::make_unexpected(url.error());
    }

    auto headers = read_json_field<std::map<std::string, std::string>, JsonFieldMode::optional_with_default>(
        json, "headers", context, {});
    if (!headers)
    {
        return nonstd::make_unexpected(headers.error());
    }

    return McpServerConfig{
        McpHttpServerConfig{
            .name = std::move(name),
            .url = std::move(*url),
            .headers = std::move(*headers),
        },
    };
}

auto parse_mcp_server(std::string name, const nlohmann::json& json) -> Result<McpServerConfig>
{
    const auto context = "MCP server " + name;
    if (!json.is_object())
    {
        return fail<McpServerConfig>(ErrorKind::InvalidArgument, context + " must be a JSON object");
    }

    auto type = read_optional_json_field<std::string>(json, "type", context);
    if (!type)
    {
        return nonstd::make_unexpected(type.error());
    }

    const auto transport = *type ? **type : std::string{"stdio"};
    if (transport == "stdio")
    {
        return parse_stdio_mcp_server(std::move(name), json, context);
    }

    if (transport == "http")
    {
        return parse_http_mcp_server(std::move(name), json, context);
    }

    return fail<McpServerConfig>(ErrorKind::InvalidArgument, context + " has unsupported type: " + transport);
}

auto mcp_servers_json(const nlohmann::json& json, const std::filesystem::path& path) -> Result<const nlohmann::json*>
{
    if (!json.is_object())
    {
        return fail<const nlohmann::json*>(
            ErrorKind::InvalidArgument, "plugin MCP config must be a JSON object: " + path.string());
    }

    for (const auto* key : {"mcpServers", "mcp_servers"})
    {
        if (json.contains(key))
        {
            const auto& servers = json.at(key);
            if (!servers.is_object())
            {
                return fail<const nlohmann::json*>(
                    ErrorKind::InvalidArgument, "plugin MCP config " + std::string{key} + " must be a JSON object: " + path.string());
            }
            return &servers;
        }
    }

    return nullptr;
}

auto load_plugin_mcp_servers(const std::filesystem::path& path) -> Result<std::vector<McpServerConfig>>
{
    std::error_code error;
    if (!std::filesystem::exists(path, error))
    {
        return std::vector<McpServerConfig>{};
    }

    auto json = read_json_file(path, "plugin MCP config");
    if (!json)
    {
        return nonstd::make_unexpected(json.error());
    }

    auto servers_json = mcp_servers_json(*json, path);
    if (!servers_json)
    {
        return nonstd::make_unexpected(servers_json.error());
    }

    if (*servers_json == nullptr)
    {
        return std::vector<McpServerConfig>{};
    }

    std::vector<McpServerConfig> servers;
    servers.reserve((*servers_json)->size());
    for (const auto& [name, server_json] : (*servers_json)->items())
    {
        auto server = parse_mcp_server(name, server_json);
        if (!server)
        {
            return nonstd::make_unexpected(server.error());
        }

        servers.push_back(std::move(*server));
    }

    return servers;
}

auto load_plugin_hooks(const std::filesystem::path& path) -> Result<std::vector<HookDefinition>>
{
    std::error_code error;
    if (!std::filesystem::exists(path, error))
    {
        return std::vector<HookDefinition>{};
    }

    auto json = read_json_file(path, "plugin hooks config");
    if (!json)
    {
        return nonstd::make_unexpected(json.error());
    }

    if (!json->is_object())
    {
        return fail<std::vector<HookDefinition>>(
            ErrorKind::InvalidArgument,
            "plugin hooks config must be a JSON object: " + path.string());
    }

    const auto hooks_json = json->find("hooks");
    if (hooks_json == json->end())
    {
        return std::vector<HookDefinition>{};
    }
    if (!hooks_json->is_array())
    {
        return fail<std::vector<HookDefinition>>(
            ErrorKind::InvalidArgument,
            "plugin hooks config hooks must be an array: " + path.string());
    }

    std::vector<HookDefinition> hooks;
    hooks.reserve(hooks_json->size());
    for (std::size_t i = 0; i < hooks_json->size(); ++i)
    {
        auto hook = hook_definition_from_json(
            hooks_json->at(i),
            fmt::format("plugin hooks {}[{}]", path.string(), i));
        if (!hook)
        {
            return nonstd::make_unexpected(hook.error());
        }
        hooks.push_back(std::move(*hook));
    }

    return hooks;
}

auto plugin_mcp_path(const LoadedPlugin& plugin) -> std::filesystem::path
{
    auto path = plugin.path / plugin.manifest.mcp_file;
    std::error_code error;
    if (std::filesystem::exists(path, error) || plugin.manifest.mcp_file != std::filesystem::path{"mcp.json"})
    {
        return path;
    }

    return plugin.path / ".mcp.json";
}

auto plugin_hooks_path(const LoadedPlugin& plugin) -> std::filesystem::path
{
    return plugin.path / plugin.manifest.hooks_file;
}

auto load_plugin(const std::filesystem::path& manifest_path) -> Result<LoadedPlugin>
{
    auto manifest = load_plugin_manifest(manifest_path);
    if (!manifest)
    {
        return nonstd::make_unexpected(manifest.error());
    }

    auto plugin = LoadedPlugin{
        .manifest = std::move(*manifest),
        .path = plugin_base_dir(manifest_path),
        .manifest_path = manifest_path,
    };

    plugin.enabled = plugin.manifest.enabled_by_default;
    if (!plugin.enabled)
    {
        return plugin;
    }

    const auto skill_root = plugin.path / plugin.manifest.skills_dir;
    const std::array skill_dirs{skill_root};
    auto skills = load_skills_from_dirs(skill_dirs, source_for_plugin(plugin.manifest));
    if (!skills)
    {
        return nonstd::make_unexpected(skills.error());
    }

    plugin.skills = std::move(*skills);

    const auto command_root = plugin.path / plugin.manifest.commands_dir;
    auto commands = load_plugin_commands(command_root, plugin.manifest);
    if (!commands)
    {
        return nonstd::make_unexpected(commands.error());
    }

    plugin.commands = std::move(*commands);

    auto mcp_servers = load_plugin_mcp_servers(plugin_mcp_path(plugin));
    if (!mcp_servers)
    {
        return nonstd::make_unexpected(mcp_servers.error());
    }

    plugin.mcp_servers = std::move(*mcp_servers);

    auto hooks = load_plugin_hooks(plugin_hooks_path(plugin));
    if (!hooks)
    {
        return nonstd::make_unexpected(hooks.error());
    }

    plugin.hooks = std::move(*hooks);
    return plugin;
}

auto discover_project_plugin_roots(
    const std::filesystem::path& cwd, std::span<const std::filesystem::path> relative_dirs)
    -> Result<std::vector<std::filesystem::path>>
{
    std::error_code error;
    const auto start = std::filesystem::weakly_canonical(cwd, error);
    if (error)
    {
        return fail<std::vector<std::filesystem::path>>(
            ErrorKind::Io, fmt::format("failed to resolve cwd: {}", error.message()));
    }

    std::vector<std::filesystem::path> roots;
    for (const auto& relative_dir : relative_dirs)
    {
        if (!is_safe_relative_path(relative_dir))
        {
            continue;
        }

        const auto candidate = start / relative_dir;
        if (!std::filesystem::is_directory(candidate, error))
        {
            continue;
        }

        auto canonical = std::filesystem::weakly_canonical(candidate, error);
        if (error)
        {
            return fail<std::vector<std::filesystem::path>>(
                ErrorKind::Io,
                fmt::format("failed to resolve plugin directory {}: {}", candidate.string(), error.message()));
        }

        roots.push_back(std::move(canonical));
    }

    return roots;
}

} // namespace

auto default_user_plugin_roots() -> std::vector<std::filesystem::path>
{
    const auto home = home_directory();
    if (!home)
    {
        return {};
    }

    return {
        *home / ".codeharness" / "plugins",
        *home / ".openharness" / "plugins",
        *home / ".agents" / "plugins",
    };
}

auto load_plugin_manifest(const std::filesystem::path& manifest_path) -> Result<PluginManifest>
{
    auto content = read_text_file(manifest_path);
    if (!content)
    {
        return nonstd::make_unexpected(content.error());
    }

    nlohmann::json json;
    try
    {
        json = nlohmann::json::parse(*content);
    }
    catch (const nlohmann::json::parse_error& error)
    {
        return fail<PluginManifest>(
            ErrorKind::InvalidArgument,
            fmt::format("failed to parse plugin manifest {}: {}", manifest_path.string(), error.what()));
    }

    return parse_manifest_json(json, manifest_path);
}

auto load_plugins(const std::filesystem::path& cwd, PluginLoadOptions options) -> Result<std::vector<LoadedPlugin>>
{
    auto roots = std::move(options.user_plugin_roots);
    if (options.load_default_user_plugins)
    {
        auto defaults = default_user_plugin_roots();
        roots.insert(roots.begin(), defaults.begin(), defaults.end());
    }

    roots.insert(roots.end(), options.extra_plugin_roots.begin(), options.extra_plugin_roots.end());

    if (options.allow_project_plugins)
    {
        auto project_roots = discover_project_plugin_roots(cwd, options.project_plugin_dirs);
        if (!project_roots)
        {
            return nonstd::make_unexpected(project_roots.error());
        }

        roots.insert(roots.end(), project_roots->begin(), project_roots->end());
    }

    auto manifests = discover_manifest_paths(roots);
    if (!manifests)
    {
        return nonstd::make_unexpected(manifests.error());
    }

    std::vector<LoadedPlugin> plugins;
    plugins.reserve(manifests->size());
    std::map<std::string, std::string> seen_mcp_servers;

    for (const auto& manifest_path : *manifests)
    {
        auto plugin = load_plugin(manifest_path);
        if (!plugin)
        {
            return nonstd::make_unexpected(plugin.error());
        }

        for (const auto& server : plugin->mcp_servers)
        {
            const auto name = std::string{mcp_server_name(server)};
            auto [existing, inserted] = seen_mcp_servers.emplace(name, plugin->manifest.name);
            if (!inserted)
            {
                return fail<std::vector<LoadedPlugin>>(
                    ErrorKind::InvalidArgument,
                    fmt::format(
                        "duplicate plugin MCP server name: {} (plugins: {}, {})",
                        name,
                        existing->second,
                        plugin->manifest.name));
            }
        }

        plugins.push_back(std::move(*plugin));
    }

    std::ranges::sort(
        plugins, [](const auto& left, const auto& right) { return left.manifest.name < right.manifest.name; });
    return plugins;
}

} // namespace codeharness
