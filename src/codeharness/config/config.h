#pragma once

#include "codeharness/hooks/hook.h"
#include "codeharness/mcp/types.h"
#include "codeharness/permissions/permission.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness::config
{

// Named provider configuration that can be referenced via settings.active_profile.
// Each profile stores connection parameters for one API provider (OpenAI, Anthropic, etc.).
struct ProviderProfile
{
    std::string name;
    std::string label;
    std::string provider_type;   // "echo" | "openai" | "anthropic"
    std::string api_format;      // optional: "openai" | "anthropic"
    std::string model;
    std::string base_url;
    std::string auth_source;     // "env:VAR" | "credentials:NAME" | "literal-key"
    std::map<std::string, std::string> extra_headers;
};

// Central runtime configuration, merged from multiple sources:
//   defaults → settings.json → environment variables → CLI flags
//
// v1 keeps this practical: only fields with actual consumers are included.
// Future additions (MemorySettings, SandboxSettings) go here when their
// corresponding subsystems need them.
struct Settings
{
    // --- Active Profile ---
    std::string active_profile = "default";
    std::map<std::string, ProviderProfile> profiles = {
        {"default", ProviderProfile{.name = "default", .label = "Default", .provider_type = "echo"}}
    };

    // --- Resolved Provider Fields ---
    // These are populated by ConfigLoader after merging all layers
    // and resolving the active profile's credentials.
    std::string provider_type = "echo";
    std::string model;
    std::string api_key;
    std::string base_url;
    int max_tokens = 4096;
    int max_turns = 200;

    // --- Permissions ---
    PermissionSettings permission;

    // --- MCP Servers ---
    std::vector<McpServerConfig> mcp_servers;

    // --- Hooks ---
    std::vector<HookDefinition> hooks;

    // --- Feature Flags ---
    bool allow_project_skills = true;
    bool allow_project_plugins = false;

    // --- Core Paths ---
    std::filesystem::path config_dir;
    std::filesystem::path data_dir;
    std::filesystem::path cwd;
    std::filesystem::path memory_root;
};

// --- JSON Serialization ---
void from_json(const nlohmann::json& j, ProviderProfile& p);
void to_json(nlohmann::json& j, const ProviderProfile& p);

void from_json(const nlohmann::json& j, Settings& s);
void to_json(nlohmann::json& j, const Settings& s);

} // namespace codeharness::config
