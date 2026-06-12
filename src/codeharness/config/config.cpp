#include "codeharness/config/config.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace codeharness::config
{

namespace
{

auto permission_mode_from_string(std::string_view value) -> PermissionMode
{
    if (value == "plan")
    {
        return PermissionMode::Plan;
    }
    if (value == "full_auto")
    {
        return PermissionMode::FullAuto;
    }

    return PermissionMode::Default;
}

auto permission_mode_to_string(PermissionMode mode) -> std::string_view
{
    switch (mode)
    {
    case PermissionMode::Plan: return "plan";
    case PermissionMode::FullAuto: return "full_auto";
    case PermissionMode::Default: return "default";
    }

    return "default";
}

auto permission_action_from_string(std::string_view value) -> PermissionAction
{
    if (value == "allow")
    {
        return PermissionAction::Allow;
    }
    if (value == "deny")
    {
        return PermissionAction::Deny;
    }

    return PermissionAction::Ask;
}

auto permission_action_to_string(PermissionAction action) -> std::string_view
{
    switch (action)
    {
    case PermissionAction::Allow: return "allow";
    case PermissionAction::Deny: return "deny";
    case PermissionAction::Ask: return "ask";
    }

    return "ask";
}

auto parse_path_rules(const nlohmann::json& rules) -> std::vector<PermissionPathRule>
{
    std::vector<PermissionPathRule> parsed;
    if (!rules.is_array())
    {
        return parsed;
    }

    for (const auto& item : rules)
    {
        if (!item.is_object())
        {
            continue;
        }

        PermissionPathRule rule;
        rule.action = permission_action_from_string(item.value("action", std::string{}));
        rule.pattern = item.value("pattern", std::string{});
        if (item.contains("tools") && item["tools"].is_array())
        {
            rule.tools = item["tools"].get<std::vector<std::string>>();
        }

        if (!rule.pattern.empty())
        {
            parsed.push_back(std::move(rule));
        }
    }

    return parsed;
}

auto parse_command_rules(const nlohmann::json& rules) -> std::vector<PermissionCommandRule>
{
    std::vector<PermissionCommandRule> parsed;
    if (!rules.is_array())
    {
        return parsed;
    }

    for (const auto& item : rules)
    {
        if (!item.is_object())
        {
            continue;
        }

        PermissionCommandRule rule;
        rule.action = permission_action_from_string(item.value("action", std::string{}));
        rule.pattern = item.value("pattern", std::string{});
        if (!rule.pattern.empty())
        {
            parsed.push_back(std::move(rule));
        }
    }

    return parsed;
}

auto path_rules_to_json(const std::vector<PermissionPathRule>& rules) -> nlohmann::json
{
    auto json = nlohmann::json::array();
    for (const auto& rule : rules)
    {
        nlohmann::json item{
            {"action", permission_action_to_string(rule.action)},
            {"pattern", rule.pattern},
        };
        if (!rule.tools.empty())
        {
            item["tools"] = rule.tools;
        }
        json.push_back(std::move(item));
    }
    return json;
}

auto command_rules_to_json(const std::vector<PermissionCommandRule>& rules) -> nlohmann::json
{
    auto json = nlohmann::json::array();
    for (const auto& rule : rules)
    {
        json.push_back(nlohmann::json{
            {"action", permission_action_to_string(rule.action)},
            {"pattern", rule.pattern},
        });
    }
    return json;
}

} // namespace

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
        if (perm.is_object())
        {
            s.permission.mode = permission_mode_from_string(perm.value("mode", std::string{}));
            if (perm.contains("allowed_tools") && perm["allowed_tools"].is_array())
            {
                s.permission.allowed_tools = perm["allowed_tools"].get<std::vector<std::string>>();
            }
            if (perm.contains("denied_tools") && perm["denied_tools"].is_array())
            {
                s.permission.denied_tools = perm["denied_tools"].get<std::vector<std::string>>();
            }
            if (perm.contains("path_rules"))
            {
                s.permission.path_rules = parse_path_rules(perm["path_rules"]);
            }
            if (perm.contains("command_rules"))
            {
                s.permission.command_rules = parse_command_rules(perm["command_rules"]);
            }
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

    if (j.contains("hooks"))
    {
        const auto& hooks_json = j["hooks"];
        if (!hooks_json.is_array())
        {
            throw nlohmann::json::type_error::create(302, "settings hooks must be an array", nullptr);
        }

        s.hooks.clear();
        for (std::size_t i = 0; i < hooks_json.size(); ++i)
        {
            auto hook = hook_definition_from_json(hooks_json[i], "settings hooks[" + std::to_string(i) + "]");
            if (!hook)
            {
                throw nlohmann::json::type_error::create(302, hook.status().message(), nullptr);
            }
            s.hooks.push_back(std::move(*hook));
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

    j["permission"] = nlohmann::json{{"mode", permission_mode_to_string(s.permission.mode)}};
    if (!s.permission.allowed_tools.empty())
    {
        j["permission"]["allowed_tools"] = s.permission.allowed_tools;
    }
    if (!s.permission.denied_tools.empty())
    {
        j["permission"]["denied_tools"] = s.permission.denied_tools;
    }
    if (!s.permission.path_rules.empty())
    {
        j["permission"]["path_rules"] = path_rules_to_json(s.permission.path_rules);
    }
    if (!s.permission.command_rules.empty())
    {
        j["permission"]["command_rules"] = command_rules_to_json(s.permission.command_rules);
    }

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

    if (!s.hooks.empty())
    {
        auto hooks = nlohmann::json::array();
        for (const auto& hook : s.hooks)
        {
            hooks.push_back(hook_definition_to_json(hook));
        }
        j["hooks"] = std::move(hooks);
    }

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
