#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <system_error>

#include "codeharness/config/config.h"
#include "codeharness/config/config_manager.h"
#include "codeharness/config/credentials.h"
#include "codeharness/config/paths.h"
#include "codeharness/config/provider_manager.h"
#include "test_support.h"

namespace ch_config = codeharness::config;

// ---- TOML Config Parsing ----

TEST_CASE("ParseTomlConfig parses basic config") {
  auto toml = R"(
default_model = "gpt-4o"
default_thinking = "off"
default_permission_mode = "manual"

[providers.my-openai]
type = "openai"
api_key = "env:OPENAI_API_KEY"
base_url = "https://api.openai.com/v1"

[providers.my-anthropic]
type = "anthropic"
api_key = "sk-ant-test"

[models]
"gpt-4o" = { provider = "my-openai", model = "gpt-4o-2024-08-06" }
"claude-sonnet-4" = { provider = "my-anthropic", model = "claude-sonnet-4-20250514" }
)";

  auto config = ch_config::ParseTomlConfig(toml);
  REQUIRE(config);

  CHECK(config->default_model == "gpt-4o");
  CHECK(config->default_thinking == "off");
  CHECK(config->default_permission_mode == "manual");

  REQUIRE(config->providers.size() == 2);
  CHECK(config->providers["my-openai"].type == "openai");
  CHECK(config->providers["my-openai"].api_key == "env:OPENAI_API_KEY");
  CHECK(config->providers["my-openai"].base_url == "https://api.openai.com/v1");
  CHECK(config->providers["my-anthropic"].type == "anthropic");
  CHECK(config->providers["my-anthropic"].api_key == "sk-ant-test");

  REQUIRE(config->models.size() == 2);
  CHECK(config->models["gpt-4o"].provider_ref == "my-openai");
  CHECK(config->models["gpt-4o"].model == "gpt-4o-2024-08-06");
  CHECK(config->models["claude-sonnet-4"].provider_ref == "my-anthropic");
  CHECK(config->models["claude-sonnet-4"].model == "claude-sonnet-4-20250514");
}

TEST_CASE("ParseTomlConfig parses permission rules") {
    auto toml = R"(
[[permission.rules]]
decision = "allow"
scope = "session-runtime"
pattern = "Bash"

[[permission.rules]]
decision = "deny"
scope = "project"
pattern = "Write(.git/**)"
)";

    auto config = ch_config::ParseTomlConfig(toml);
    REQUIRE(config);

    REQUIRE(config->permission_rules.size() == 2);
    CHECK(config->permission_rules[0].decision == "allow");
    CHECK(config->permission_rules[0].scope == "session-runtime");
    CHECK(config->permission_rules[0].pattern == "Bash");
    CHECK(config->permission_rules[1].decision == "deny");
    CHECK(config->permission_rules[1].scope == "project");
    CHECK(config->permission_rules[1].pattern == "Write(.git/**)");
}

TEST_CASE("ParseTomlConfig parses MCP servers") {
  auto toml = R"(
[mcp_servers]
[mcp_servers.test-server]
transport = "stdio"
command = "node"
args = ["server.js"]
env = { KEY = "VALUE" }

[mcp_servers.remote-api]
transport = "http"
url = "https://mcp.example.com"
headers = { Authorization = "Bearer token" }
)";

  auto config = ch_config::ParseTomlConfig(toml);
  REQUIRE(config);

  REQUIRE(config->mcp_servers.size() == 2);

  const auto* stdio = std::get_if<codeharness::McpStdioServerConfig>(&config->mcp_servers[0]);
  REQUIRE(stdio != nullptr);
  CHECK(stdio->name == "test-server");
  CHECK(stdio->command == "node");
  REQUIRE(stdio->args.size() == 1);
  CHECK(stdio->args[0] == "server.js");
  CHECK(stdio->env.at("KEY") == "VALUE");

  const auto* http = std::get_if<codeharness::McpHttpServerConfig>(&config->mcp_servers[1]);
  REQUIRE(http != nullptr);
  CHECK(http->name == "remote-api");
  CHECK(http->url == "https://mcp.example.com");
  CHECK(http->headers.at("Authorization") == "Bearer token");
}

TEST_CASE("ParseTomlConfig parses hooks") {
  auto toml = R"(
[[hooks]]
event = "pre_tool_use"
type = "command"
priority = 25
matcher = "write_file"
block_on_failure = true
timeout_seconds = 7
command = "echo hook"
)";

  auto config = ch_config::ParseTomlConfig(toml);
  REQUIRE(config);

  REQUIRE(config->hooks.size() == 1);
  CHECK(config->hooks[0].event == codeharness::HookEvent::PreToolUse);
  CHECK(config->hooks[0].type == codeharness::HookType::Command);
  CHECK(config->hooks[0].priority == 25);
  REQUIRE(config->hooks[0].matcher.has_value());
  CHECK(*config->hooks[0].matcher == "write_file");
  CHECK(config->hooks[0].block_on_failure);
  CHECK(config->hooks[0].timeout_seconds == 7);
}

TEST_CASE("ParseTomlConfig returns error on invalid TOML") {
  auto result = ch_config::ParseTomlConfig("this is not toml = {{broken}");
  CHECK(!result);
}

TEST_CASE("ParseTomlConfig returns error on missing provider type") {
  auto toml = R"(
[providers.bad]
api_key = "test"
)";
  auto result = ch_config::ParseTomlConfig(toml);
  CHECK(!result);
}

TEST_CASE("ParseTomlConfig returns error on missing model provider ref") {
  auto toml = R"(
[providers.ok]
type = "openai"

[models]
bad = { provider = "" }
)";
  auto result = ch_config::ParseTomlConfig(toml);
  CHECK(!result);
}

// ---- TOML Serialization Round-Trip ----

TEST_CASE("SerializeToToml round-trips config") {
  ch_config::CodeHarnessConfig config;
  config.default_model = "gpt-4o";
  config.default_thinking = "off";
  config.default_permission_mode = "manual";

  ch_config::ProviderConfig pc;
  pc.type = "openai";
  pc.api_key = "env:OPENAI_API_KEY";
  config.providers["my-openai"] = std::move(pc);

  ch_config::ModelAlias ma;
  ma.provider_ref = "my-openai";
  ma.model = "gpt-4o-2024-08-06";
  config.models["gpt-4o"] = std::move(ma);

  auto toml_result = ch_config::SerializeToToml(config);
  REQUIRE(toml_result);

  auto parsed = ch_config::ParseTomlConfig(*toml_result);
  REQUIRE(parsed);

  CHECK(parsed->default_model == "gpt-4o");
  CHECK(parsed->default_thinking == "off");
  CHECK(parsed->default_permission_mode == "manual");
  REQUIRE(parsed->providers.size() == 1);
  CHECK(parsed->providers["my-openai"].type == "openai");
  CHECK(parsed->providers["my-openai"].api_key == "env:OPENAI_API_KEY");
  REQUIRE(parsed->models.size() == 1);
  CHECK(parsed->models["gpt-4o"].provider_ref == "my-openai");
  CHECK(parsed->models["gpt-4o"].model == "gpt-4o-2024-08-06");
}

// ---- ConfigManager ----

TEST_CASE("ConfigManager loads and validates config") {
  TempDir temp{"cfg-mgr-load"};
  auto config_dir = temp.path;
  auto config_path = config_dir / "config.toml";

  write_file(config_path, R"(
[providers.my-openai]
type = "openai"
api_key = "env:OPENAI_API_KEY"

[models]
"gpt-4o" = { provider = "my-openai", model = "gpt-4o-2024-08-06" }
)");

  ch_config::ConfigManager mgr(config_dir);
  auto config = mgr.Load();
  REQUIRE(config);
  CHECK(config->config_dir == config_dir);
  CHECK(config->data_dir == ch_config::data_dir());
  REQUIRE(config->providers.size() == 1);
  CHECK(config->providers["my-openai"].type == "openai");
}

TEST_CASE("ConfigManager returns NotFound on missing file") {
  TempDir temp{"cfg-mgr-notfound"};
  ch_config::ConfigManager mgr(temp.path);
  auto config = mgr.Load();
  CHECK(!config);
  CHECK(absl::IsNotFound(config.status()));
}

TEST_CASE("ConfigManager Validate rejects bad model refs") {
  ch_config::CodeHarnessConfig config;
  config.models["bad"] = ch_config::ModelAlias{
      .provider_ref = "nonexistent",
      .model = "test",
  };

  ch_config::ConfigManager mgr(std::filesystem::current_path());
  auto validation = mgr.Validate(config);
  CHECK(!validation);
}

TEST_CASE("ConfigManager Validate passes with valid refs") {
  ch_config::CodeHarnessConfig config;
  config.providers["ok"] = ch_config::ProviderConfig{.type = "openai"};
  config.models["good"] = ch_config::ModelAlias{
      .provider_ref = "ok",
      .model = "test",
  };

  ch_config::ConfigManager mgr(std::filesystem::current_path());
  auto validation = mgr.Validate(config);
  CHECK(validation.ok());
}

// ---- ProviderManager ----

TEST_CASE("ProviderManager resolves model alias to provider") {
  ch_config::CodeHarnessConfig config;
  config.providers["my-anthropic"] = ch_config::ProviderConfig{
      .type = "anthropic",
      .api_key = "sk-test-key",
  };
  config.models["claude"] = ch_config::ModelAlias{
      .provider_ref = "my-anthropic",
      .model = "claude-sonnet-4",
  };

  TempDir temp{"pm-resolve"};
  ch_config::ProviderManager pm(config, temp.path);
  auto resolved = pm.ResolveProviderForModel("claude");
  REQUIRE(resolved);
  CHECK(resolved->type == "anthropic");
  CHECK(resolved->api_key == "sk-test-key");
}

TEST_CASE("ProviderManager resolves direct provider name") {
  ch_config::CodeHarnessConfig config;
  config.providers["direct"] = ch_config::ProviderConfig{
      .type = "openai",
      .api_key = "env:OPENAI_API_KEY",
  };

  TempDir temp{"pm-direct"};
  ch_config::ProviderManager pm(config, temp.path);
  auto resolved = pm.ResolveProvider("direct");
  REQUIRE(resolved);
  CHECK(resolved->type == "openai");
}

TEST_CASE("ProviderManager returns NotFound for unknown provider") {
  ch_config::CodeHarnessConfig config;
  TempDir temp{"pm-unknown"};
  ch_config::ProviderManager pm(config, temp.path);
  auto resolved = pm.ResolveProvider("nonexistent");
  CHECK(!resolved);
  CHECK(absl::IsNotFound(resolved.status()));
}

TEST_CASE("ProviderManager lists model names") {
  ch_config::CodeHarnessConfig config;
  config.providers["p"] = ch_config::ProviderConfig{.type = "openai"};
  config.models["m1"] = ch_config::ModelAlias{.provider_ref = "p", .model = "m1"};
  config.models["m2"] = ch_config::ModelAlias{.provider_ref = "p", .model = "m2"};

  TempDir temp{"pm-list"};
  ch_config::ProviderManager pm(config, temp.path);
  auto names = pm.ListModelNames();
  CHECK(names.size() == 2);
}

// ---- Credentials (preserved from original) ----

TEST_CASE("mask_api_key masks long keys correctly") {
  CHECK(ch_config::mask_api_key("sk-ant-abc123def456") == "sk-ant-***");
}

TEST_CASE("mask_api_key handles short keys") { CHECK(ch_config::mask_api_key("abc") == "abc***"); }

TEST_CASE("mask_api_key handles empty key") { CHECK(ch_config::mask_api_key("") == "***"); }

TEST_CASE("load_credentials returns empty on missing file") {
  TempDir temp{"creds-missing"};
  auto creds = ch_config::load_credentials(temp.path);
  CHECK(creds);
  CHECK(creds->profile_keys.empty());
}

TEST_CASE("load_credentials parses valid file") {
  TempDir temp{"creds-parse"};
  write_file(temp.path / "credentials.json", R"({
        "profiles": {
            "default": { "api_key": "sk-ant-real-key" },
            "work": { "api_key": "sk-proj-work-key" }
        }
    })");

  auto creds = ch_config::load_credentials(temp.path);
  REQUIRE(creds);
  REQUIRE(creds->profile_keys.size() == 2);
  CHECK(creds->profile_keys["default"] == "sk-ant-real-key");
  CHECK(creds->profile_keys["work"] == "sk-proj-work-key");
}

TEST_CASE("load_credentials returns error on invalid JSON") {
  TempDir temp{"creds-invalid"};
  write_file(temp.path / "credentials.json", "this is not json");

  auto creds = ch_config::load_credentials(temp.path);
  CHECK(!creds);
}

TEST_CASE("resolve_api_key handles env: prefix") {
  auto* existing = std::getenv("PATH");
  auto expected = existing ? std::string{existing} : std::string{};

  ch_config::Credentials creds;
  auto key = ch_config::resolve_api_key("env:PATH", "openai", creds);
  CHECK(key == expected);
}

TEST_CASE("resolve_api_key handles credentials: prefix") {
  ch_config::Credentials creds;
  creds.profile_keys["my-profile"] = "sk-my-key";

  auto key = ch_config::resolve_api_key("credentials:my-profile", "openai", creds);
  CHECK(key == "sk-my-key");
}

TEST_CASE("resolve_api_key handles literal key") {
  ch_config::Credentials creds;
  auto key = ch_config::resolve_api_key("sk-literal-key", "openai", creds);
  CHECK(key == "sk-literal-key");
}

// ---- Paths ----

TEST_CASE("config_dir defaults to HOME/.codeharness") {
  auto dir = ch_config::config_dir();
  CHECK(!dir.empty());
  CHECK(dir.filename() == ".codeharness");
}

TEST_CASE("data_dir defaults to config_dir/data") {
  auto conf = ch_config::config_dir();
  auto data = ch_config::data_dir();
  CHECK(data == conf / "data");
}

TEST_CASE("path helpers return expected subdirectories") {
  auto conf = ch_config::config_dir();
  auto data = ch_config::data_dir();

  CHECK(ch_config::skills_dir() == conf / "skills");
  CHECK(ch_config::plugins_dir() == conf / "plugins");
  CHECK(ch_config::agents_dir() == conf / "agents");
  CHECK(ch_config::sessions_dir() == data / "sessions");
  CHECK(ch_config::memory_dir() == data / "memory");
  CHECK(ch_config::tasks_dir() == data / "tasks");
  CHECK(ch_config::tool_artifacts_dir() == data / "tool_artifacts");
}

TEST_CASE("config_file and credentials_file return expected paths") {
  auto conf = ch_config::config_dir();
  CHECK(ch_config::config_file() == conf / "config.toml");
  CHECK(ch_config::credentials_file() == conf / "credentials.json");
}
