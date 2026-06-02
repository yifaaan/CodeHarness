#include "codeharness/plugins/plugin_loader.h"

#include <glob/glob.h>
#include <nlohmann/json.hpp>
#include <nonstd/expected.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "codeharness/core/json_parse.h"
#include "codeharness/skills/skill_loader.h"
#include "codeharness/tools/text_file.h"

namespace codeharness
{

namespace
{

auto home_directory() -> std::optional<std::filesystem::path>
{
#ifdef _WIN32
    const auto* home = std::getenv("USERPROFILE");
#else
    const auto* home = std::getenv("HOME");
#endif

    if (home == nullptr || *home == '\0')
    {
        return std::nullopt;
    }

    return std::filesystem::path{home};
}

auto has_parent_reference(const std::filesystem::path& path) -> bool
{
    return std::ranges::any_of(path, [](const auto& part) { return part == ".."; });
}

auto is_safe_relative_path(const std::filesystem::path& path) -> bool
{
    return !path.empty() && !path.is_absolute() && !path.has_root_name() && !has_parent_reference(path);
}

auto parse_manifest_json(const nlohmann::json& json, const std::filesystem::path& manifest_path)
    -> Result<PluginManifest>
{
    if (!json.is_object())
    {
        return fail<PluginManifest>(
            ErrorKind::InvalidArgument,
            "plugin manifest must be a JSON object: " + manifest_path.string());
    }

    auto name = read_json_field<std::string>(json, "name", "plugin manifest");
    if (!name)
    {
        return nonstd::make_unexpected(name.error());
    }

    auto manifest = PluginManifest{.name = std::move(*name)};

    auto version = read_optional_json_field<std::string>(json, "version", "plugin manifest");
    if (!version)
    {
        return nonstd::make_unexpected(version.error());
    }
    if (*version)
    {
        manifest.version = std::move(**version);
    }

    auto description = read_optional_json_field<std::string>(json, "description", "plugin manifest");
    if (!description)
    {
        return nonstd::make_unexpected(description.error());
    }
    if (*description)
    {
        manifest.description = std::move(**description);
    }

    auto enabled = read_optional_json_field<bool>(json, "enabled_by_default", "plugin manifest");
    if (!enabled)
    {
        return nonstd::make_unexpected(enabled.error());
    }
    if (*enabled)
    {
        manifest.enabled_by_default = **enabled;
    }

    auto enabled_camel = read_optional_json_field<bool>(json, "enabledByDefault", "plugin manifest");
    if (!enabled_camel)
    {
        return nonstd::make_unexpected(enabled_camel.error());
    }
    if (*enabled_camel)
    {
        manifest.enabled_by_default = **enabled_camel;
    }

    auto skills_dir = read_optional_json_field<std::string>(json, "skills_dir", "plugin manifest");
    if (!skills_dir)
    {
        return nonstd::make_unexpected(skills_dir.error());
    }
    if (*skills_dir)
    {
        manifest.skills_dir = **skills_dir;
    }

    auto skills_dir_camel = read_optional_json_field<std::string>(json, "skillsDir", "plugin manifest");
    if (!skills_dir_camel)
    {
        return nonstd::make_unexpected(skills_dir_camel.error());
    }
    if (*skills_dir_camel)
    {
        manifest.skills_dir = **skills_dir_camel;
    }

    auto commands_dir = read_optional_json_field<std::string>(json, "commands_dir", "plugin manifest");
    if (!commands_dir)
    {
        return nonstd::make_unexpected(commands_dir.error());
    }
    if (*commands_dir)
    {
        manifest.commands_dir = **commands_dir;
    }

    auto commands_dir_camel = read_optional_json_field<std::string>(json, "commandsDir", "plugin manifest");
    if (!commands_dir_camel)
    {
        return nonstd::make_unexpected(commands_dir_camel.error());
    }
    if (*commands_dir_camel)
    {
        manifest.commands_dir = **commands_dir_camel;
    }

    auto mcp_file = read_optional_json_field<std::string>(json, "mcp_file", "plugin manifest");
    if (!mcp_file)
    {
        return nonstd::make_unexpected(mcp_file.error());
    }
    if (*mcp_file)
    {
        manifest.mcp_file = **mcp_file;
    }

    auto mcp_file_camel = read_optional_json_field<std::string>(json, "mcpFile", "plugin manifest");
    if (!mcp_file_camel)
    {
        return nonstd::make_unexpected(mcp_file_camel.error());
    }
    if (*mcp_file_camel)
    {
        manifest.mcp_file = **mcp_file_camel;
    }

    if (!is_safe_relative_path(manifest.skills_dir))
    {
        return fail<PluginManifest>(
            ErrorKind::InvalidArgument,
            "plugin skills_dir must be a safe relative path: " + manifest_path.string());
    }

    if (!is_safe_relative_path(manifest.commands_dir))
    {
        return fail<PluginManifest>(
            ErrorKind::InvalidArgument,
            "plugin commands_dir must be a safe relative path: " + manifest_path.string());
    }

    if (!is_safe_relative_path(manifest.mcp_file))
    {
        return fail<PluginManifest>(
            ErrorKind::InvalidArgument,
            "plugin mcp_file must be a safe relative path: " + manifest_path.string());
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
                ErrorKind::Io,
                fmt::format("failed to scan plugin root {}: {}", root.string(), e.what()));
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
            ErrorKind::Io,
            fmt::format("failed to scan plugin command directory {}: {}", root.string(), e.what()));
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
            ErrorKind::InvalidArgument,
            fmt::format("failed to parse {} {}: {}", label, path.string(), error.what()));
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

    auto args = read_json_field<std::vector<std::string>, JsonFieldMode::optional_with_default>(
        json,
        "args",
        context,
        {});
    if (!args)
    {
        return nonstd::make_unexpected(args.error());
    }

    auto env = read_json_field<std::map<std::string, std::string>, JsonFieldMode::optional_with_default>(
        json,
        "env",
        context,
        {});
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
        json,
        "headers",
        context,
        {});
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
            ErrorKind::InvalidArgument,
            "plugin MCP config must be a JSON object: " + path.string());
    }

    if (json.contains("mcpServers"))
    {
        const auto& servers = json.at("mcpServers");
        if (!servers.is_object())
        {
            return fail<const nlohmann::json*>(
                ErrorKind::InvalidArgument,
                "plugin MCP config mcpServers must be a JSON object: " + path.string());
        }

        return &servers;
    }

    if (json.contains("mcp_servers"))
    {
        const auto& servers = json.at("mcp_servers");
        if (!servers.is_object())
        {
            return fail<const nlohmann::json*>(
                ErrorKind::InvalidArgument,
                "plugin MCP config mcp_servers must be a JSON object: " + path.string());
        }

        return &servers;
    }

    return static_cast<const nlohmann::json*>(nullptr);
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
    return plugin;
}

auto discover_project_plugin_roots(const std::filesystem::path& cwd,
                                   std::span<const std::filesystem::path> relative_dirs)
    -> Result<std::vector<std::filesystem::path>>
{
    std::error_code error;
    const auto start = std::filesystem::weakly_canonical(cwd, error);
    if (error)
    {
        return fail<std::vector<std::filesystem::path>>(
            ErrorKind::Io,
            fmt::format("failed to resolve cwd: {}", error.message()));
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

    for (const auto& manifest_path : *manifests)
    {
        auto plugin = load_plugin(manifest_path);
        if (!plugin)
        {
            return nonstd::make_unexpected(plugin.error());
        }

        plugins.push_back(std::move(*plugin));
    }

    std::ranges::sort(plugins, [](const auto& left, const auto& right) {
        return left.manifest.name < right.manifest.name;
    });
    return plugins;
}

} // namespace codeharness
