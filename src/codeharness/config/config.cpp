#include "codeharness/config/config.h"

#include <spdlog/spdlog.h>
#include <toml++/toml.h>

#include <sstream>
#include <string>

namespace codeharness::config {

namespace {

absl::StatusOr<ProviderConfig> ParseProviderConfig(const toml::table& tbl, const std::string& name) {
  ProviderConfig pc;
  pc.type = tbl["type"].value_or(std::string{});
  if (pc.type.empty()) {
    return absl::InvalidArgumentError("provider '" + name + "' missing required field 'type'");
  }
  pc.api_key = tbl["api_key"].value_or(std::string{});
  pc.base_url = tbl["base_url"].value_or(std::string{});

  if (tbl.contains("extra_headers")) {
    const auto* headers = tbl["extra_headers"].as_table();
    if (headers) {
      for (const auto& [key, val] : *headers) {
        pc.extra_headers[std::string{key.str()}] = val.value_or(std::string{});
      }
    }
  }

  return pc;
}

absl::StatusOr<ModelAlias> ParseModelAlias(const toml::table& tbl, const std::string& name) {
  ModelAlias ma;
  ma.provider_ref = tbl["provider"].value_or(std::string{});
  if (ma.provider_ref.empty()) {
    return absl::InvalidArgumentError("model '" + name + "' missing required field 'provider'");
  }
  ma.model = tbl["model"].value_or(std::string{});
  return ma;
}

PermissionRule ParsePermissionRule(const toml::table& tbl) {
  PermissionRule rule;
  rule.decision = tbl["decision"].value_or(std::string{});
  rule.scope = tbl["scope"].value_or(std::string{});
  rule.pattern = tbl["pattern"].value_or(std::string{});
  return rule;
}

absl::StatusOr<HookDefinition> ParseHookEvent(const toml::table& tbl) {
  HookDefinition hd;

  auto event_str = tbl["event"].value_or(std::string{});
  auto event_opt = hook_event_from_string(event_str);
  if (!event_opt) {
    return absl::InvalidArgumentError("unknown hook event: " + event_str);
  }
  hd.event = *event_opt;

  auto type_str = tbl["type"].value_or(std::string{});
  auto type_opt = hook_type_from_string(type_str);
  if (!type_opt) {
    return absl::InvalidArgumentError("unknown hook type: " + type_str);
  }
  hd.type = *type_opt;

  hd.priority = tbl["priority"].value_or(0);
  hd.block_on_failure = tbl["block_on_failure"].value_or(false);
  hd.timeout_seconds = tbl["timeout_seconds"].value_or(30);

  auto matcher = tbl["matcher"].value_or(std::string{});
  if (!matcher.empty()) {
    hd.matcher = std::move(matcher);
  }

  nlohmann::json cfg = nlohmann::json::object();
  cfg["command"] = tbl["command"].value_or(std::string{});
  if (!cfg["command"].empty()) {
    hd.config = std::move(cfg);
  }

  return hd;
}

absl::StatusOr<McpServerConfig> ParseMcpServer(const toml::table& tbl, const std::string& name) {
  auto transport = tbl["transport"].value_or(std::string{"stdio"});

  if (transport == "stdio") {
    McpStdioServerConfig cfg;
    cfg.name = name;
    cfg.command = tbl["command"].value_or(std::string{});
    if (cfg.command.empty()) {
      return absl::InvalidArgumentError("MCP server '" + name + "' missing required field 'command'");
    }

    const auto* args_arr = tbl["args"].as_array();
    if (args_arr) {
      for (const auto& arg : *args_arr) {
        cfg.args.push_back(arg.value_or(std::string{}));
      }
    }

    const auto* env_tbl = tbl["env"].as_table();
    if (env_tbl) {
      for (const auto& [key, val] : *env_tbl) {
        cfg.env[std::string{key.str()}] = val.value_or(std::string{});
      }
    }

    return McpServerConfig{std::move(cfg)};
  }

  if (transport == "http" || transport == "sse") {
    McpHttpServerConfig cfg;
    cfg.name = name;
    cfg.url = tbl["url"].value_or(std::string{});
    if (cfg.url.empty()) {
      return absl::InvalidArgumentError("MCP server '" + name + "' missing required field 'url'");
    }

    const auto* headers_tbl = tbl["headers"].as_table();
    if (headers_tbl) {
      for (const auto& [key, val] : *headers_tbl) {
        cfg.headers[std::string{key.str()}] = val.value_or(std::string{});
      }
    }

    return McpServerConfig{std::move(cfg)};
  }

  return absl::InvalidArgumentError("MCP server '" + name + "' unknown transport: " + transport);
}

}  // namespace

absl::StatusOr<CodeHarnessConfig> ParseTomlConfig(std::string_view toml_content) {
  CodeHarnessConfig config;

  try {
    auto tbl = toml::parse(std::string{toml_content});

    config.default_model = tbl["default_model"].value_or(std::string{});
    config.default_thinking = tbl["default_thinking"].value_or(std::string{});
    config.default_permission_mode = tbl["default_permission_mode"].value_or(std::string{});

    // Parse [providers]
    const auto* providers_tbl = tbl["providers"].as_table();
    if (providers_tbl) {
      for (const auto& [key, val] : *providers_tbl) {
        const auto* provider_tbl = val.as_table();
        if (!provider_tbl) continue;

        auto pc = ParseProviderConfig(*provider_tbl, std::string{key.str()});
        if (!pc) return pc.status();

        config.providers[std::string{key.str()}] = std::move(*pc);
      }
    }

    // Parse [models]
    const auto* models_tbl = tbl["models"].as_table();
    if (models_tbl) {
      for (const auto& [key, val] : *models_tbl) {
        const auto* model_tbl = val.as_table();
        if (!model_tbl) continue;

        auto ma = ParseModelAlias(*model_tbl, std::string{key.str()});
        if (!ma) return ma.status();

        config.models[std::string{key.str()}] = std::move(*ma);
      }
    }

    // Parse [[permission.rules]]
    const auto* rules_arr = tbl["permission"]["rules"].as_array();
    if (rules_arr) {
      for (const auto& item : *rules_arr) {
        const auto* rule_tbl = item.as_table();
        if (!rule_tbl) continue;

        config.permission_rules.push_back(ParsePermissionRule(*rule_tbl));
      }
    }

    // Parse [[hooks]]
    const auto* hooks_arr = tbl["hooks"].as_array();
    if (hooks_arr) {
      for (const auto& item : *hooks_arr) {
        const auto* hook_tbl = item.as_table();
        if (!hook_tbl) continue;

        auto hd = ParseHookEvent(*hook_tbl);
        if (!hd) return hd.status();

        config.hooks.push_back(std::move(*hd));
      }
    }

    // Parse [mcp_servers]
    const auto* mcp_tbl = tbl["mcp_servers"].as_table();
    if (mcp_tbl) {
      for (const auto& [key, val] : *mcp_tbl) {
        const auto* server_tbl = val.as_table();
        if (!server_tbl) continue;

        auto server = ParseMcpServer(*server_tbl, std::string{key.str()});
        if (!server) return server.status();

        config.mcp_servers.push_back(std::move(*server));
      }
    }
  } catch (const toml::parse_error& e) {
    return absl::FailedPreconditionError("TOML parse error: " + std::string{e.what()});
  }

  return config;
}

absl::StatusOr<std::string> SerializeToToml(const CodeHarnessConfig& config) {
  try {
    toml::table tbl;

    if (!config.default_model.empty()) tbl.emplace("default_model", config.default_model);
    if (!config.default_thinking.empty()) tbl.emplace("default_thinking", config.default_thinking);
    if (!config.default_permission_mode.empty()) tbl.emplace("default_permission_mode", config.default_permission_mode);

    // [providers]
    toml::table providers_tbl;
    for (const auto& [name, pc] : config.providers) {
      toml::table provider_tbl;
      provider_tbl.emplace("type", pc.type);
      if (!pc.api_key.empty()) provider_tbl.emplace("api_key", pc.api_key);
      if (!pc.base_url.empty()) provider_tbl.emplace("base_url", pc.base_url);

      toml::table headers_tbl;
      for (const auto& [key, value] : pc.extra_headers) headers_tbl.emplace(key, value);
      if (!headers_tbl.empty()) provider_tbl.emplace("extra_headers", std::move(headers_tbl));

      providers_tbl.emplace(name, std::move(provider_tbl));
    }
    if (!providers_tbl.empty()) tbl.emplace("providers", std::move(providers_tbl));

    // [models]
    toml::table models_tbl;
    for (const auto& [name, ma] : config.models) {
      toml::table model_tbl;
      model_tbl.emplace("provider", ma.provider_ref);
      model_tbl.emplace("model", ma.model);
      models_tbl.emplace(name, std::move(model_tbl));
    }
    if (!models_tbl.empty()) tbl.emplace("models", std::move(models_tbl));

    // [[permission.rules]]
    if (!config.permission_rules.empty()) {
      toml::array rules_arr;
      for (const auto& rule : config.permission_rules) {
        toml::table rule_tbl;
        if (!rule.decision.empty()) rule_tbl.emplace("decision", rule.decision);
        if (!rule.scope.empty()) rule_tbl.emplace("scope", rule.scope);
        if (!rule.pattern.empty()) rule_tbl.emplace("pattern", rule.pattern);
        rules_arr.push_back(std::move(rule_tbl));
      }

      toml::table permission_tbl;
      permission_tbl.emplace("rules", std::move(rules_arr));
      tbl.emplace("permission", std::move(permission_tbl));
    }

    // [[hooks]]
    if (!config.hooks.empty()) {
      toml::array hooks_arr;
      for (const auto& hook : config.hooks) {
        toml::table hook_tbl;
        hook_tbl.emplace("event", hook_event_to_string(hook.event));
        hook_tbl.emplace("type", hook_type_to_string(hook.type));
        if (hook.priority != 0) hook_tbl.emplace("priority", hook.priority);
        if (hook.matcher) hook_tbl.emplace("matcher", *hook.matcher);
        if (hook.block_on_failure) hook_tbl.emplace("block_on_failure", true);
        if (hook.timeout_seconds != 30) hook_tbl.emplace("timeout_seconds", hook.timeout_seconds);
        if (hook.config.contains("command")) hook_tbl.emplace("command", hook.config["command"].get<std::string>());
        hooks_arr.push_back(std::move(hook_tbl));
      }
      tbl.emplace("hooks", std::move(hooks_arr));
    }

    // [mcp_servers]
    if (!config.mcp_servers.empty()) {
      toml::table mcp_tbl;
      for (const auto& server : config.mcp_servers) {
        toml::table server_tbl;

        std::visit(
            [&](const auto& cfg) {
              using T = std::decay_t<decltype(cfg)>;
              if constexpr (std::is_same_v<T, McpStdioServerConfig>) {
                server_tbl.emplace("transport", "stdio");
                server_tbl.emplace("command", cfg.command);
                if (!cfg.args.empty()) {
                  toml::array args_arr;
                  for (const auto& arg : cfg.args) args_arr.push_back(arg);
                  server_tbl.emplace("args", std::move(args_arr));
                }
                if (!cfg.env.empty()) {
                  toml::table env_tbl;
                  for (const auto& [key, val] : cfg.env) env_tbl.emplace(key, val);
                  server_tbl.emplace("env", std::move(env_tbl));
                }
              } else {
                server_tbl.emplace("transport", "http");
                server_tbl.emplace("url", cfg.url);
                if (!cfg.headers.empty()) {
                  toml::table headers_tbl;
                  for (const auto& [key, val] : cfg.headers) headers_tbl.emplace(key, val);
                  server_tbl.emplace("headers", std::move(headers_tbl));
                }
              }
            },
            server);

        mcp_tbl.emplace(mcp_server_name(server), std::move(server_tbl));
      }
      tbl.emplace("mcp_servers", std::move(mcp_tbl));
    }

    std::ostringstream oss;
    oss << tbl << "\n";
    return oss.str();
  } catch (const std::exception& e) {
    return absl::InternalError("TOML serialization error: " + std::string{e.what()});
  }
}

}  // namespace codeharness::config
