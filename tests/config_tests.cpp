#include "codeharness/config/config.h"
#include "codeharness/config/config_loader.h"
#include "codeharness/config/credentials.h"
#include "codeharness/config/paths.h"
#include "test_support.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace ch_config = codeharness::config;

// ---- Defaults ----

TEST_CASE("config defaults have echo provider")
{
    ch_config::Settings s;
    CHECK(s.provider_type == "echo");
    CHECK(s.active_profile == "default");
    CHECK(s.max_turns == 200);
    CHECK(s.max_tokens == 4096);
    CHECK(s.allow_project_skills == true);
    CHECK(s.allow_project_plugins == false);
    CHECK(s.permission.mode == codeharness::PermissionMode::Default);
}

TEST_CASE("config defaults have a default profile")
{
    ch_config::Settings s;
    REQUIRE(s.profiles.find("default") != s.profiles.end());
    CHECK(s.profiles["default"].name == "default");
    CHECK(s.profiles["default"].provider_type == "echo");
}

// ---- JSON Serialization ----

TEST_CASE("settings JSON round-trip preserves fields")
{
    ch_config::Settings s;
    s.provider_type = "openai";
    s.model = "gpt-4";
    s.base_url = "https://api.openai.com/v1";
    s.max_turns = 50;
    s.max_tokens = 2048;
    s.allow_project_skills = false;
    s.allow_project_plugins = true;

    // Serialize
    nlohmann::json j = s;

    // Deserialize
    auto s2 = j.get<ch_config::Settings>();

    CHECK(s2.provider_type == "openai");
    CHECK(s2.model == "gpt-4");
    CHECK(s2.base_url == "https://api.openai.com/v1");
    CHECK(s2.max_turns == 50);
    CHECK(s2.max_tokens == 2048);
    CHECK(s2.allow_project_skills == false);
    CHECK(s2.allow_project_plugins == true);
}

TEST_CASE("settings JSON does not serialize api_key")
{
    ch_config::Settings s;
    s.api_key = "sk-secret-123";

    nlohmann::json j = s;
    CHECK(!j.contains("api_key"));
}

TEST_CASE("settings JSON parses permission modes")
{
    auto test_mode = [](const std::string& mode_str, codeharness::PermissionMode expected) {
        auto json = nlohmann::json::parse(R"({ "permission": { "mode": ")" + mode_str + R"(" } })");
        auto s = json.get<ch_config::Settings>();
        CHECK(s.permission.mode == expected);
    };

    test_mode("default", codeharness::PermissionMode::Default);
    test_mode("plan", codeharness::PermissionMode::Plan);
    test_mode("full_auto", codeharness::PermissionMode::FullAuto);
}

TEST_CASE("settings JSON parses permission rule settings")
{
    auto json = nlohmann::json::parse(R"({
        "permission": {
            "mode": "default",
            "allowed_tools": ["read_file"],
            "denied_tools": ["bash"],
            "path_rules": [
                {"action": "allow", "pattern": "src/**", "tools": ["edit_file", "write_file"]},
                {"action": "deny", "pattern": ".git/**"}
            ],
            "command_rules": [
                {"action": "deny", "pattern": "\\bgit\\s+push\\b"}
            ]
        }
    })");

    auto s = json.get<ch_config::Settings>();

    CHECK(s.permission.allowed_tools == std::vector<std::string>{"read_file"});
    CHECK(s.permission.denied_tools == std::vector<std::string>{"bash"});
    REQUIRE(s.permission.path_rules.size() == 2);
    CHECK(s.permission.path_rules[0].action == codeharness::PermissionAction::Allow);
    CHECK(s.permission.path_rules[0].pattern == "src/**");
    CHECK(s.permission.path_rules[0].tools == std::vector<std::string>{"edit_file", "write_file"});
    CHECK(s.permission.path_rules[1].action == codeharness::PermissionAction::Deny);
    CHECK(s.permission.path_rules[1].pattern == ".git/**");
    REQUIRE(s.permission.command_rules.size() == 1);
    CHECK(s.permission.command_rules[0].action == codeharness::PermissionAction::Deny);
    CHECK(s.permission.command_rules[0].pattern == R"(\bgit\s+push\b)");
}

TEST_CASE("settings JSON serializes permission rule settings")
{
    ch_config::Settings s;
    s.permission.mode = codeharness::PermissionMode::FullAuto;
    s.permission.allowed_tools = {"read_file"};
    s.permission.denied_tools = {"bash"};
    s.permission.path_rules.push_back(codeharness::PermissionPathRule{
        .action = codeharness::PermissionAction::Allow,
        .pattern = "src/**",
        .tools = {"write_file"},
    });
    s.permission.command_rules.push_back(codeharness::PermissionCommandRule{
        .action = codeharness::PermissionAction::Deny,
        .pattern = R"(\bgit\s+push\b)",
    });

    nlohmann::json j = s;

    CHECK(j["permission"]["mode"] == "full_auto");
    CHECK(j["permission"]["allowed_tools"] == nlohmann::json::array({"read_file"}));
    CHECK(j["permission"]["denied_tools"] == nlohmann::json::array({"bash"}));
    CHECK(j["permission"]["path_rules"][0]["action"] == "allow");
    CHECK(j["permission"]["path_rules"][0]["pattern"] == "src/**");
    CHECK(j["permission"]["path_rules"][0]["tools"] == nlohmann::json::array({"write_file"}));
    CHECK(j["permission"]["command_rules"][0]["action"] == "deny");
    CHECK(j["permission"]["command_rules"][0]["pattern"] == R"(\bgit\s+push\b)");
}

TEST_CASE("settings JSON parses MCP servers")
{
    auto json = nlohmann::json::parse(R"({
        "mcp_servers": [
            { "transport": "stdio", "name": "test-server", "command": "node", "args": ["server.js"] },
            { "transport": "http", "name": "remote-api", "url": "https://mcp.example.com" }
        ]
    })");
    auto s = json.get<ch_config::Settings>();

    REQUIRE(s.mcp_servers.size() == 2);

    const auto* stdio = std::get_if<codeharness::McpStdioServerConfig>(&s.mcp_servers[0]);
    REQUIRE(stdio != nullptr);
    CHECK(stdio->name == "test-server");
    CHECK(stdio->command == "node");

    const auto* http = std::get_if<codeharness::McpHttpServerConfig>(&s.mcp_servers[1]);
    REQUIRE(http != nullptr);
    CHECK(http->name == "remote-api");
    CHECK(http->url == "https://mcp.example.com");
}

TEST_CASE("settings JSON parses and serializes command hooks")
{
    auto json = nlohmann::json::parse(R"({
        "hooks": [
            {
                "event": "pre_tool_use",
                "type": "command",
                "priority": 25,
                "matcher": "write_file",
                "block_on_failure": true,
                "timeout_seconds": 7,
                "config": {"command": "echo hook"}
            }
        ]
    })");

    auto s = json.get<ch_config::Settings>();

    REQUIRE(s.hooks.size() == 1);
    CHECK(s.hooks[0].event == codeharness::HookEvent::PreToolUse);
    CHECK(s.hooks[0].type == codeharness::HookType::Command);
    CHECK(s.hooks[0].priority == 25);
    REQUIRE(s.hooks[0].matcher.has_value());
    CHECK(*s.hooks[0].matcher == "write_file");
    CHECK(s.hooks[0].block_on_failure);
    CHECK(s.hooks[0].timeout_seconds == 7);
    CHECK(s.hooks[0].config.at("command") == "echo hook");

    nlohmann::json serialized = s;
    CHECK(serialized["hooks"][0]["event"] == "pre_tool_use");
    CHECK(serialized["hooks"][0]["type"] == "command");
    CHECK(serialized["hooks"][0]["matcher"] == "write_file");
    CHECK(serialized["hooks"][0]["config"]["command"] == "echo hook");
}

TEST_CASE("settings JSON rejects unsupported hook type")
{
    auto json = nlohmann::json::parse(R"({
        "hooks": [
            {"event": "pre_tool_use", "type": "http", "config": {"command": "echo hook"}}
        ]
    })");

    CHECK_THROWS_AS(json.get<ch_config::Settings>(), nlohmann::json::type_error);
}

TEST_CASE("provider profile JSON round-trip")
{
    ch_config::ProviderProfile p;
    p.name = "work";
    p.label = "Work Account";
    p.provider_type = "anthropic";
    p.model = "claude-sonnet-4-6";
    p.base_url = "https://api.anthropic.com";
    p.auth_source = "env:ANTHROPIC_API_KEY";
    p.extra_headers = {{"X-Api-Version", "2023-06-01"}};

    nlohmann::json j = p;
    auto p2 = j.get<ch_config::ProviderProfile>();

    CHECK(p2.name == "work");
    CHECK(p2.label == "Work Account");
    CHECK(p2.provider_type == "anthropic");
    CHECK(p2.model == "claude-sonnet-4-6");
    CHECK(p2.base_url == "https://api.anthropic.com");
    CHECK(p2.auth_source == "env:ANTHROPIC_API_KEY");
    CHECK(p2.extra_headers.size() == 1);
    CHECK(p2.extra_headers["X-Api-Version"] == "2023-06-01");
}

// ---- Credentials ----

TEST_CASE("mask_api_key masks long keys correctly")
{
    CHECK(ch_config::mask_api_key("sk-ant-abc123def456") == "sk-ant-***");
}

TEST_CASE("mask_api_key handles short keys")
{
    CHECK(ch_config::mask_api_key("abc") == "abc***");
}

TEST_CASE("mask_api_key handles empty key")
{
    CHECK(ch_config::mask_api_key("") == "***");
}

TEST_CASE("load_credentials returns empty on missing file")
{
    TempDir temp{"creds-missing"};
    auto creds = ch_config::load_credentials(temp.path);
    CHECK(creds);
    CHECK(creds->profile_keys.empty());
}

TEST_CASE("load_credentials parses valid file")
{
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

TEST_CASE("load_credentials returns error on invalid JSON")
{
    TempDir temp{"creds-invalid"};
    write_file(temp.path / "credentials.json", "this is not json");

    auto creds = ch_config::load_credentials(temp.path);
    CHECK(!creds);
    CHECK(creds.error().kind == codeharness::ErrorKind::Config);
}

TEST_CASE("resolve_api_key handles env: prefix")
{
    // Use a known env var for testing.
    auto* existing = std::getenv("PATH");
    auto expected = existing ? std::string{existing} : std::string{};

    ch_config::Credentials creds;
    auto key = ch_config::resolve_api_key("env:PATH", "openai", creds);
    CHECK(key == expected);
}

TEST_CASE("resolve_api_key handles credentials: prefix")
{
    ch_config::Credentials creds;
    creds.profile_keys["my-profile"] = "sk-my-key";

    auto key = ch_config::resolve_api_key("credentials:my-profile", "openai", creds);
    CHECK(key == "sk-my-key");
}

TEST_CASE("resolve_api_key handles missing credentials profile")
{
    ch_config::Credentials creds;
    auto key = ch_config::resolve_api_key("credentials:nonexistent", "openai", creds);
    CHECK(key.empty());
}

TEST_CASE("resolve_api_key handles literal key")
{
    ch_config::Credentials creds;
    auto key = ch_config::resolve_api_key("sk-literal-key", "openai", creds);
    CHECK(key == "sk-literal-key");
}

TEST_CASE("resolve_api_key falls back to provider env var when auth_source is empty")
{
    // We can only reliably test that neither OPENAI_API_KEY nor ANTHROPIC_API_KEY
    // is set, and expect empty. We'll just check it doesn't crash.
    ch_config::Credentials creds;
    auto key = ch_config::resolve_api_key("", "echo", creds);
    CHECK(key.empty());
}

// ---- ConfigLoader ----

TEST_CASE("ConfigLoader defaults match expected values")
{
    ch_config::ConfigLoader loader;
    auto settings = loader.load(ch_config::CliOptions{});
    REQUIRE(settings);

    CHECK(settings->provider_type == "echo");
    CHECK(settings->max_turns == 200);
    CHECK(settings->allow_project_skills == true);
    CHECK(settings->allow_project_plugins == false);
    CHECK(settings->permission.mode == codeharness::PermissionMode::Default);
}

TEST_CASE("ConfigLoader CLI overrides provider_type")
{
    ch_config::ConfigLoader loader;
    auto settings = loader.load(ch_config::CliOptions{
        .provider_type = "openai",
    });
    REQUIRE(settings);
    CHECK(settings->provider_type == "openai");
}

TEST_CASE("ConfigLoader CLI overrides model, api_key, base_url")
{
    ch_config::ConfigLoader loader;
    auto settings = loader.load(ch_config::CliOptions{
        .provider_type = "anthropic",
        .model = "claude-sonnet-4-6",
        .api_key = "sk-ant-test",
        .base_url = "https://api.anthropic.com",
    });
    REQUIRE(settings);
    CHECK(settings->provider_type == "anthropic");
    CHECK(settings->model == "claude-sonnet-4-6");
    CHECK(settings->api_key == "sk-ant-test");
    CHECK(settings->base_url == "https://api.anthropic.com");
}

TEST_CASE("ConfigLoader CLI overrides max_turns")
{
    ch_config::ConfigLoader loader;
    auto settings = loader.load(ch_config::CliOptions{
        .max_turns = 42,
    });
    REQUIRE(settings);
    CHECK(settings->max_turns == 42);
}

TEST_CASE("ConfigLoader CLI overrides profile")
{
    ch_config::ConfigLoader loader;
    auto settings = loader.load(ch_config::CliOptions{
        .profile = "nonexistent",
    });
    REQUIRE(settings);
    // Should still load (profile just won't be found, which is not an error).
    CHECK(settings->active_profile == "nonexistent");
}

// ---- ConfigLoader with settings.json file ----

TEST_CASE("ConfigLoader reads settings.json file")
{
    TempDir temp{"loader-file"};
    auto config_dir = temp.path;
    write_file(config_dir / "settings.json", R"({
        "provider_type": "openai",
        "model": "gpt-4-turbo",
        "max_turns": 100,
        "allow_project_skills": false
    })");

    // Override config_dir via env
    auto old_env = std::getenv("CODEHARNESS_CONFIG_DIR");
    auto set_env = std::string{"CODEHARNESS_CONFIG_DIR="} + config_dir.string();
    _putenv(set_env.c_str());

    ch_config::ConfigLoader loader;
    auto settings = loader.load(ch_config::CliOptions{});
    REQUIRE(settings);

    CHECK(settings->provider_type == "openai");
    CHECK(settings->model == "gpt-4-turbo");
    CHECK(settings->max_turns == 100);
    CHECK(settings->allow_project_skills == false);

    // Restore env
    if (old_env)
    {
        auto restore = std::string{"CODEHARNESS_CONFIG_DIR="} + old_env;
        _putenv(restore.c_str());
    }
    else
    {
        _putenv("CODEHARNESS_CONFIG_DIR=");
    }
}

TEST_CASE("ConfigLoader reads full permission settings from file")
{
    TempDir temp{"loader-permission-rules"};
    auto config_dir = temp.path;
    write_file(config_dir / "settings.json", R"({
        "permission": {
            "mode": "full_auto",
            "allowed_tools": ["read_file"],
            "path_rules": [{"action": "deny", "pattern": ".git/**"}],
            "command_rules": [{"action": "deny", "pattern": "\\bgit\\s+push\\b"}]
        }
    })");

    auto old_env = std::getenv("CODEHARNESS_CONFIG_DIR");
    auto set_env = std::string{"CODEHARNESS_CONFIG_DIR="} + config_dir.string();
    _putenv(set_env.c_str());

    ch_config::ConfigLoader loader;
    auto settings = loader.load(ch_config::CliOptions{});
    REQUIRE(settings);

    CHECK(settings->permission.mode == codeharness::PermissionMode::FullAuto);
    CHECK(settings->permission.allowed_tools == std::vector<std::string>{"read_file"});
    REQUIRE(settings->permission.path_rules.size() == 1);
    CHECK(settings->permission.path_rules[0].pattern == ".git/**");
    REQUIRE(settings->permission.command_rules.size() == 1);
    CHECK(settings->permission.command_rules[0].pattern == R"(\bgit\s+push\b)");

    if (old_env)
    {
        auto restore = std::string{"CODEHARNESS_CONFIG_DIR="} + old_env;
        _putenv(restore.c_str());
    }
    else
    {
        _putenv("CODEHARNESS_CONFIG_DIR=");
    }
}

TEST_CASE("ConfigLoader reads hooks from file")
{
    TempDir temp{"loader-hooks"};
    auto config_dir = temp.path;
    write_file(config_dir / "settings.json", R"({
        "hooks": [
            {
                "event": "post_tool_use",
                "type": "command",
                "matcher": "write_file",
                "config": {"command": "echo post"}
            }
        ]
    })");

    auto old_env = std::getenv("CODEHARNESS_CONFIG_DIR");
    auto set_env = std::string{"CODEHARNESS_CONFIG_DIR="} + config_dir.string();
    _putenv(set_env.c_str());

    ch_config::ConfigLoader loader;
    auto settings = loader.load(ch_config::CliOptions{});
    REQUIRE(settings);

    REQUIRE(settings->hooks.size() == 1);
    CHECK(settings->hooks[0].event == codeharness::HookEvent::PostToolUse);
    REQUIRE(settings->hooks[0].matcher.has_value());
    CHECK(*settings->hooks[0].matcher == "write_file");

    if (old_env)
    {
        auto restore = std::string{"CODEHARNESS_CONFIG_DIR="} + old_env;
        _putenv(restore.c_str());
    }
    else
    {
        _putenv("CODEHARNESS_CONFIG_DIR=");
    }
}

TEST_CASE("ConfigLoader file with invalid JSON returns error")
{
    // Create a temp settings.json with invalid content.
    TempDir temp{"loader-invalid"};
    write_file(temp.path / "settings.json", "{invalid json}");

    auto old_env = std::getenv("CODEHARNESS_CONFIG_DIR");
    auto set_env = std::string{"CODEHARNESS_CONFIG_DIR="} + temp.path.string();
    _putenv(set_env.c_str());

    ch_config::ConfigLoader loader;
    auto settings = loader.load(ch_config::CliOptions{});
    CHECK(!settings);
    if (!settings)
    {
        CHECK(settings.error().kind == codeharness::ErrorKind::Config);
    }

    if (old_env)
    {
        auto restore = std::string{"CODEHARNESS_CONFIG_DIR="} + old_env;
        _putenv(restore.c_str());
    }
    else
    {
        _putenv("CODEHARNESS_CONFIG_DIR=");
    }
}

// ---- Merge Priority ----

TEST_CASE("CLI overrides env var")
{
    TempDir temp{"merge-cli-env"};
    auto config_dir = temp.path;
    write_file(config_dir / "settings.json", R"({ "provider_type": "openai" })");

    auto old_env_config = std::getenv("CODEHARNESS_CONFIG_DIR");
    auto set_env = std::string{"CODEHARNESS_CONFIG_DIR="} + config_dir.string();
    _putenv(set_env.c_str());

    auto old_env_provider = std::getenv("CODEHARNESS_PROVIDER");
    _putenv("CODEHARNESS_PROVIDER=anthropic");

    ch_config::ConfigLoader loader;
    auto settings = loader.load(ch_config::CliOptions{
        .provider_type = "echo",
    });
    REQUIRE(settings);
    // CLI should win over env
    CHECK(settings->provider_type == "echo");

    // Restore env
    _putenv("CODEHARNESS_PROVIDER=");
    if (old_env_provider)
    {
        auto restore = std::string{"CODEHARNESS_PROVIDER="} + old_env_provider;
        _putenv(restore.c_str());
    }
    if (old_env_config)
    {
        auto restore = std::string{"CODEHARNESS_CONFIG_DIR="} + old_env_config;
        _putenv(restore.c_str());
    }
    else
    {
        _putenv("CODEHARNESS_CONFIG_DIR=");
    }
}

// ---- Paths ----

TEST_CASE("config_dir defaults to HOME/.codeharness")
{
    auto dir = ch_config::config_dir();
    CHECK(!dir.empty());
    CHECK(dir.filename() == ".codeharness");
}

TEST_CASE("data_dir defaults to config_dir/data")
{
    auto conf = ch_config::config_dir();
    auto data = ch_config::data_dir();
    CHECK(data == conf / "data");
}

TEST_CASE("path helpers return expected subdirectories")
{
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

TEST_CASE("settings_file and credentials_file return expected paths")
{
    auto conf = ch_config::config_dir();
    CHECK(ch_config::settings_file() == conf / "settings.json");
    CHECK(ch_config::credentials_file() == conf / "credentials.json");
}

// ---- Smoke test: loader handles empty CLI gracefully ----

TEST_CASE("ConfigLoader with empty CliOptions succeeds")
{
    ch_config::ConfigLoader loader;
    auto settings = loader.load(ch_config::CliOptions{});
    REQUIRE(settings);
    CHECK(settings->provider_type == "echo");
}
