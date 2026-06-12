#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "codeharness/hooks/hook.h"
#include "codeharness/mcp/types.h"
#include "codeharness/permissions/permission.h"

namespace codeharness::config {

// Provider configuration from TOML [providers.<name>]
//   [providers.my-anthropic]
//   type = "anthropic"
//   api_key = "env:ANTHROPIC_API_KEY"
struct ProviderConfig {
  std::string type;     // "anthropic" | "openai" | "google" | "echo"
  std::string api_key;  // literal, "env:VAR", or "credentials:NAME"
  std::string base_url;
  std::map<std::string, std::string> extra_headers;
};

// Model alias: maps a friendly name to a (provider, model) pair.
//   [models]
//   "claude-sonnet-4" = { provider = "my-anthropic", model = "claude-sonnet-4-20250514" }
struct ModelAlias {
  std::string provider_ref;  // references a [providers] name
  std::string model;         // actual model name passed to the API
};

// Permission rule from [[permission.rules]]
//   [[permission.rules]]
//   decision = "allow"
//   scope = "session-runtime"
//   pattern = "Bash"
struct PermissionRule {
  std::string decision;  // "allow" | "deny" | "ask"
  std::string scope;     // "session-runtime" | "turn-override" | "project" | "user"
  std::string pattern;   // ToolName(pattern)
};

// Top-level CodeHarness configuration, typically loaded from config.toml.
struct CodeHarnessConfig {
  std::string default_model;
  std::string default_thinking;
  std::string default_permission_mode;

  std::map<std::string, ProviderConfig> providers;
  std::map<std::string, ModelAlias> models;
  std::vector<PermissionRule> permission_rules;

  std::vector<HookDefinition> hooks;
  std::vector<McpServerConfig> mcp_servers;

  std::filesystem::path config_dir;
  std::filesystem::path data_dir;
};

// Parse TOML content into CodeHarnessConfig.
absl::StatusOr<CodeHarnessConfig> ParseTomlConfig(std::string_view toml_content);

// Serialize CodeHarnessConfig to TOML string.
absl::StatusOr<std::string> SerializeToToml(const CodeHarnessConfig& config);

}  // namespace codeharness::config
