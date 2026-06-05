#include "codeharness/config/config.h"

#include <nlohmann/json.hpp>

namespace codeharness::config
{

// ---- ProviderProfile ----

void from_json(const nlohmann::json& j, ProviderProfile& p)
{
    p.name = j.value("name", p.name);
    p.label = j.value("label", p.name);
    p.provider_type = j.value("provider_type", p.provider_type);
    p.api_format = j.value("api_format", p.api_format);
    p.model = j.value("model", p.model);
    p.base_url = j.value("base_url", p.base_url);
    p.auth_source = j.value("auth_source", p.auth_source);
    if (j.contains("extra_headers") && j["extra_headers"].is_object())
    {
        p.extra_headers = j["extra_headers"].get<std::map<std::string, std::string>>();
    }
}

void to_json(nlohmann::json& j, const ProviderProfile& p)
{
    j = nlohmann::json{
        {"name", p.name},
        {"label", p.label},
        {"provider_type", p.provider_type},
        {"model", p.model},
        {"base_url", p.base_url},
        {"auth_source", p.auth_source},
    };
    if (!p.api_format.empty())
    {
        j["api_format"] = p.api_format;
    }
    if (!p.extra_headers.empty())
    {
        j["extra_headers"] = p.extra_headers;
    }
}

// ---- Settings ----

void from_json(const nlohmann::json& j, Settings& s)
{
    s.active_profile = j.value("active_profile", s.active_profile);

    if (j.contains("profiles") && j["profiles"].is_object())
    {
        for (auto& [key, value] : j["profiles"].items())
        {
            s.profiles[key] = value.get<ProviderProfile>();
        }
    }

    s.provider_type = j.value("provider_type", s.provider_type);
    s.model = j.value("model", s.model);
    s.base_url = j.value("base_url", s.base_url);
    s.max_tokens = j.value("max_tokens", s.max_tokens);
    s.max_turns = j.value("max_turns", s.max_turns);

    if (j.contains("permission"))
    {
        // PermissionSettings from_json: map "mode" string → PermissionMode enum
        const auto& perm = j["permission"];
        auto mode_str = perm.value("mode", std::string{});
        if (mode_str == "plan")
        {
            s.permission.mode = PermissionMode::Plan;
        }
        else if (mode_str == "full_auto")
        {
            s.permission.mode = PermissionMode::FullAuto;
        }
        else
        {
            s.permission.mode = PermissionMode::Default;
        }
    }

    if (j.contains("mcp_servers") && j["mcp_servers"].is_array())
    {
        for (const auto& server_json : j["mcp_servers"])
        {
            auto type = server_json.value("transport", std::string{});
            if (type == "stdio" || type.empty())
            {
                McpStdioServerConfig cfg;
                cfg.name = server_json.value("name", cfg.name);
                cfg.command = server_json.value("command", cfg.command);
                cfg.args = server_json.value("args", cfg.args);
                if (server_json.contains("env") && server_json["env"].is_object())
                {
                    cfg.env = server_json["env"].get<std::map<std::string, std::string>>();
                }
                s.mcp_servers.push_back(std::move(cfg));
            }
            else if (type == "http")
            {
                McpHttpServerConfig cfg;
                cfg.name = server_json.value("name", cfg.name);
                cfg.url = server_json.value("url", cfg.url);
                if (server_json.contains("headers") && server_json["headers"].is_object())
                {
                    cfg.headers = server_json["headers"].get<std::map<std::string, std::string>>();
                }
                s.mcp_servers.push_back(std::move(cfg));
            }
        }
    }

    s.allow_project_skills = j.value("allow_project_skills", s.allow_project_skills);
    s.allow_project_plugins = j.value("allow_project_plugins", s.allow_project_plugins);

    s.config_dir = j.value("config_dir", s.config_dir);
    s.data_dir = j.value("data_dir", s.data_dir);
    s.memory_root = j.value("memory_root", s.memory_root);
}

void to_json(nlohmann::json& j, const Settings& s)
{
    j = nlohmann::json{
        {"active_profile", s.active_profile},
        {"profiles", s.profiles},
        {"provider_type", s.provider_type},
        {"model", s.model},
        {"base_url", s.base_url},
        {"max_tokens", s.max_tokens},
        {"max_turns", s.max_turns},
        {"allow_project_skills", s.allow_project_skills},
        {"allow_project_plugins", s.allow_project_plugins},
    };

    // Permission
    std::string mode_str = "default";
    switch (s.permission.mode)
    {
    case PermissionMode::Plan: mode_str = "plan"; break;
    case PermissionMode::FullAuto: mode_str = "full_auto"; break;
    default: break;
    }
    j["permission"] = nlohmann::json{{"mode", mode_str}};

    // MCP servers
    nlohmann::json servers = nlohmann::json::array();
    for (const auto& server : s.mcp_servers)
    {
        if (auto* stdio = std::get_if<McpStdioServerConfig>(&server))
        {
            nlohmann::json sj;
            sj["transport"] = "stdio";
            sj["name"] = stdio->name;
            sj["command"] = stdio->command;
            sj["args"] = stdio->args;
            if (!stdio->env.empty())
            {
                sj["env"] = stdio->env;
            }
            servers.push_back(std::move(sj));
        }
        else if (auto* http = std::get_if<McpHttpServerConfig>(&server))
        {
            nlohmann::json sj;
            sj["transport"] = "http";
            sj["name"] = http->name;
            sj["url"] = http->url;
            if (!http->headers.empty())
            {
                sj["headers"] = http->headers;
            }
            servers.push_back(std::move(sj));
        }
    }
    j["mcp_servers"] = std::move(servers);

    // Paths (only when non-empty)
    if (!s.config_dir.empty())
    {
        j["config_dir"] = s.config_dir.string();
    }
    if (!s.data_dir.empty())
    {
        j["data_dir"] = s.data_dir.string();
    }
    if (!s.memory_root.empty())
    {
        j["memory_root"] = s.memory_root.string();
    }

    // Never serialize api_key.
}

} // namespace codeharness::config
