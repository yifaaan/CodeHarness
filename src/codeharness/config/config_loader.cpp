#include "codeharness/config/config_loader.h"
#include "codeharness/config/paths.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <fstream>
#include <string>

namespace codeharness::config
{

ConfigLoader::ConfigLoader() = default;

auto ConfigLoader::load(const CliOptions& cli) -> absl::StatusOr<Settings>
{
    // Layer 1: defaults
    auto settings = load_defaults();

    // Layer 2: config file
    auto config_file = settings.config_dir / "settings.json";
    {
        auto file_result = load_file(config_file);
        if (!file_result)
        {
            // File not found is not an error; other errors propagate.
            // load_file returns success with defaults on missing file.
            return file_result.error();
        }
        // Merge file values onto defaults.
        auto& file_settings = *file_result;
        if (!file_settings.active_profile.empty())
        {
            settings.active_profile = std::move(file_settings.active_profile);
        }
        if (!file_settings.profiles.empty())
        {
            // Merge — file profiles override defaults with the same key.
            for (auto& [key, profile] : file_settings.profiles)
            {
                settings.profiles[key] = std::move(profile);
            }
        }
        if (!file_settings.provider_type.empty())
        {
            settings.provider_type = std::move(file_settings.provider_type);
        }
        if (!file_settings.model.empty())
        {
            settings.model = std::move(file_settings.model);
        }
        if (!file_settings.base_url.empty())
        {
            settings.base_url = std::move(file_settings.base_url);
        }
        if (file_settings.max_tokens != 4096)
        {
            settings.max_tokens = file_settings.max_tokens;
        }
        if (file_settings.max_turns != 200)
        {
            settings.max_turns = file_settings.max_turns;
        }
        if (!file_settings.mcp_servers.empty())
        {
            settings.mcp_servers = std::move(file_settings.mcp_servers);
        }
        if (!file_settings.hooks.empty())
        {
            settings.hooks = std::move(file_settings.hooks);
        }
        if (!file_settings.config_dir.empty())
        {
            settings.config_dir = std::move(file_settings.config_dir);
        }
        if (!file_settings.data_dir.empty())
        {
            settings.data_dir = std::move(file_settings.data_dir);
        }
        if (!file_settings.memory_root.empty())
        {
            settings.memory_root = std::move(file_settings.memory_root);
        }
        // Allow file to set memory_root, config_dir, data_dir.

        // Boolean feature flags: always merge from file.
        settings.allow_project_skills = file_settings.allow_project_skills;
        settings.allow_project_plugins = file_settings.allow_project_plugins;

        if (file_settings.permission.mode != PermissionMode::Default ||
            !file_settings.permission.allowed_tools.empty() ||
            !file_settings.permission.denied_tools.empty() ||
            !file_settings.permission.path_rules.empty() ||
            !file_settings.permission.command_rules.empty())
        {
            settings.permission = std::move(file_settings.permission);
        }
    }

    // Layer 3: environment variables
    apply_env(settings);

    // Layer 4: CLI flags
    apply_cli(settings, cli);

    // Resolve active profile after all layers have been merged so that
    // profile fields don't needlessly override env/CLI values, and so
    // the credentials file is only loaded when genuinely necessary.
    resolve_active_profile(settings);

    return settings;
}

auto ConfigLoader::load_defaults() -> Settings
{
    Settings s;
    s.active_profile = "default";
    s.provider_type = "openai";
    s.max_tokens = 4096;
    s.max_turns = 200;
    s.allow_project_skills = true;
    s.allow_project_plugins = false;
    s.permission.mode = PermissionMode::Default;
    s.config_dir = config::config_dir();
    s.data_dir = config::data_dir();
    s.memory_root = config::memory_dir();

    // Create a default profile.
    ProviderProfile default_profile;
    default_profile.name = "default";
    default_profile.label = "Default";
    default_profile.provider_type = "openai";
    s.profiles["default"] = std::move(default_profile);

    return s;
}

auto ConfigLoader::load_file(const std::filesystem::path& path) -> absl::StatusOr<Settings>
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        // File not found → return default Settings (not an error).
        return Settings{};
    }

    try
    {
        auto json = nlohmann::json::parse(file);
        auto s = json.get<Settings>();
        spdlog::debug("loaded settings from {}", path.string());
        return s;
    }
    catch (const nlohmann::json::exception& e)
    {
        return absl::StatusOr<Settings>(absl::FailedPreconditionError("failed to parse " + path.string()) + ": " + e.what());
    }
}

void ConfigLoader::apply_env(Settings& settings)
{
    auto read_env = [](const char* var) -> std::string {
        auto* value = std::getenv(var);
        return value ? std::string{value} : std::string{};
    };

    auto val = read_env("CODEHARNESS_PROVIDER");
    if (!val.empty()) { settings.provider_type = std::move(val); }

    val = read_env("CODEHARNESS_MODEL");
    if (!val.empty()) { settings.model = std::move(val); }

    val = read_env("CODEHARNESS_API_KEY");
    if (!val.empty()) { settings.api_key = std::move(val); }

    val = read_env("CODEHARNESS_BASE_URL");
    if (!val.empty()) { settings.base_url = std::move(val); }

    val = read_env("CODEHARNESS_MAX_TURNS");
    if (!val.empty())
    {
        try
        {
            int parsed = std::stoi(val);
            if (parsed > 0) { settings.max_turns = parsed; }
        }
        catch (const std::exception&)
        {
            spdlog::warn("invalid CODEHARNESS_MAX_TURNS value: {}", val);
        }
    }

    val = read_env("CODEHARNESS_CONFIG_DIR");
    if (!val.empty()) { settings.config_dir = std::filesystem::path{val}; }

    val = read_env("CODEHARNESS_DATA_DIR");
    if (!val.empty()) { settings.data_dir = std::filesystem::path{val}; }

    val = read_env("CODEHARNESS_PROFILE");
    if (!val.empty()) { settings.active_profile = std::move(val); }

    val = read_env("CODEHARNESS_MEMORY_ROOT");
    if (!val.empty()) { settings.memory_root = std::filesystem::path{val}; }
}

void ConfigLoader::apply_cli(Settings& settings, const CliOptions& cli)
{
    if (!cli.provider_type.empty())
    {
        settings.provider_type = cli.provider_type;
    }
    if (!cli.model.empty())
    {
        settings.model = cli.model;
    }
    if (!cli.api_key.empty())
    {
        settings.api_key = cli.api_key;
    }
    if (!cli.base_url.empty())
    {
        settings.base_url = cli.base_url;
    }
    if (cli.max_turns > 0)
    {
        settings.max_turns = cli.max_turns;
    }
    if (!cli.cwd.empty())
    {
        settings.cwd = cli.cwd;
    }
    if (!cli.profile.empty())
    {
        settings.active_profile = cli.profile;
    }
}

void ConfigLoader::resolve_active_profile(Settings& settings)
{
    auto it = settings.profiles.find(settings.active_profile);
    if (it == settings.profiles.end())
    {
        spdlog::warn("active profile '{}' not found, using defaults", settings.active_profile);
        return;
    }

    const auto& profile = it->second;

    // Promote profile fields — only overwrite empty top-level fields
    // (file/env/CLI values already set take priority).
    if (!profile.provider_type.empty() && settings.provider_type.empty())
    {
        settings.provider_type = profile.provider_type;
    }
    if (!profile.model.empty() && settings.model.empty())
    {
        settings.model = profile.model;
    }
    if (!profile.base_url.empty() && settings.base_url.empty())
    {
        settings.base_url = profile.base_url;
    }

    // Resolve API key.
    // If api_key is already set (from env/CLI), don't overwrite.
    if (settings.api_key.empty())
    {
        // Cache credentials on first resolve.
        if (!cached_credentials_.ok())
        {
            auto creds = load_credentials(settings.config_dir);
            if (creds)
            {
                cached_credentials_ = std::move(*creds);
            }
            else
            {
                spdlog::warn("failed to load credentials: {}", creds.status().message());
                cached_credentials_ = Credentials{};
            }
        }

        settings.api_key = resolve_api_key(profile.auth_source, profile.provider_type, *cached_credentials_);
    }
}

} // namespace codeharness::config
