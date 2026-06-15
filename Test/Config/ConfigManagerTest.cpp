#include "Config/ConfigManager.h"

#include <absl/status/statusor.h>
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "Host/LocalHost.h"
#include "Mcp/McpTypes.h"

namespace config = codeharness::config;
namespace host = codeharness::host;

namespace
{

	// RAII guard that sets a process env var for the duration of a test and
	// restores (or unsets) it on destruction. doctest runs sequentially by
	// default, so this is safe across test cases.
	struct ScopedEnv
	{
		std::string name;
		bool had = false;
		std::string oldVal;

		ScopedEnv(std::string n, std::string v) : name(std::move(n))
		{
			if (const char* cur = std::getenv(name.c_str()))
			{
				had = true;
				oldVal = cur;
			}
#ifdef _WIN32
			_putenv_s(name.c_str(), v.c_str());
#else
			setenv(name.c_str(), v.c_str(), 1);
#endif
		}
		~ScopedEnv()
		{
			if (had)
			{
#ifdef _WIN32
				_putenv_s(name.c_str(), oldVal.c_str());
#else
				setenv(name.c_str(), oldVal.c_str(), 1);
#endif
			}
			else
			{
#ifdef _WIN32
				_putenv_s(name.c_str(), "");
#else
				unsetenv(name.c_str());
#endif
			}
		}
	};

	config::KimiConfig SampleConfig()
	{
		config::KimiConfig cfg;
		cfg.defaultModel = "gpt-4o";
		cfg.defaultThinking = false;
		cfg.defaultPermissionMode = config::PermissionMode::Manual;
		config::ProviderConfig pc;
		pc.type = config::ProviderType::OpenAi;
		pc.apiKey = "sk-test";
		cfg.providers["openai"] = pc;
		config::ModelAlias m;
		m.provider = "openai";
		m.model = "gpt-4o";
		cfg.models["gpt-4o"] = m;
		return cfg;
	}

} // namespace

TEST_CASE("ConfigManager: LoadFromString parses general fields")
{
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(
default_model = "claude-sonnet-4"
default_thinking = true
default_permission_mode = "auto"
)");
	REQUIRE(cfg.ok());
	CHECK(cfg->defaultModel == "claude-sonnet-4");
	CHECK(cfg->defaultThinking == true);
	CHECK(cfg->defaultPermissionMode == config::PermissionMode::Auto);
}

TEST_CASE("ConfigManager: LoadFromString parses providers and models")
{
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(
[providers.my-openai]
type = "openai"
api_key = "sk-proj-abc"
base_url = "https://my-proxy.example.com/v1"

[models."gpt-4o"]
provider = "my-openai"
model = "gpt-4o"
)");
	REQUIRE(cfg.ok());
	REQUIRE(cfg->providers.size() == 1);
	const auto& pc = cfg->providers["my-openai"];
	CHECK(pc.type == config::ProviderType::OpenAi);
	REQUIRE(pc.apiKey.has_value());
	CHECK(*pc.apiKey == "sk-proj-abc");
	REQUIRE(pc.baseUrl.has_value());
	CHECK(*pc.baseUrl == "https://my-proxy.example.com/v1");
	REQUIRE(cfg->models.size() == 1);
	CHECK(cfg->models["gpt-4o"].provider == "my-openai");
	CHECK(cfg->models["gpt-4o"].model == "gpt-4o");
}

TEST_CASE("ConfigManager: parses env sub-table as secondary credential source")
{
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(
[providers.openai]
type = "openai"

[providers.openai.env]
OPENAI_API_KEY = "sk-from-env"
OPENAI_BASE_URL = "https://proxy.example.com/v1"
)");
	REQUIRE(cfg.ok());
	REQUIRE(cfg->providers["openai"].env.size() == 2);
	CHECK(cfg->providers["openai"].env["OPENAI_API_KEY"] == "sk-from-env");
}

TEST_CASE("ConfigManager: api_key expands $VAR from process env")
{
	ScopedEnv guard("CODEHARNESS_TEST_KEY", "expanded-secret");
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(
[providers.openai]
type = "openai"
api_key = "$CODEHARNESS_TEST_KEY"
)");
	REQUIRE(cfg.ok());
	REQUIRE(cfg->providers["openai"].apiKey.has_value());
	CHECK(*cfg->providers["openai"].apiKey == "expanded-secret");
}

TEST_CASE("ConfigManager: api_key expands ${VAR} form")
{
	ScopedEnv guard("CODEHARNESS_TEST_KEY2", "braced-secret");
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(
[providers.openai]
type = "openai"
api_key = "${CODEHARNESS_TEST_KEY2}"
)");
	REQUIRE(cfg.ok());
	CHECK(*cfg->providers["openai"].apiKey == "braced-secret");
}

TEST_CASE("ConfigManager: unset $VAR expands to empty")
{
	ScopedEnv guard("CODEHARNESS_DEFINITELY_UNSET_VAR_X9", "");
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(
[providers.openai]
type = "openai"
api_key = "$CODEHARNESS_DEFINITELY_UNSET_VAR_X9"
)");
	REQUIRE(cfg.ok());
	CHECK(cfg->providers["openai"].apiKey->empty());
}

TEST_CASE("ConfigManager: parses thinking block")
{
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(
[thinking]
effort = "high"
budget_tokens = 4096
)");
	REQUIRE(cfg.ok());
	REQUIRE(cfg->thinking.has_value());
	REQUIRE(cfg->thinking->effort.has_value());
	CHECK(*cfg->thinking->effort == codeharness::llm::ThinkingEffort::High);
	REQUIRE(cfg->thinking->budgetTokens.has_value());
	CHECK(*cfg->thinking->budgetTokens == 4096);
}

TEST_CASE("ConfigManager: Validate flags dangling provider reference")
{
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(
[models."gpt-4o"]
provider = "nonexistent"
model = "gpt-4o"
)");
	REQUIRE(cfg.ok());
	auto st = mgr.Validate(*cfg);
	CHECK_FALSE(st.ok());
	CHECK(st.code() == absl::StatusCode::kFailedPrecondition);
}

TEST_CASE("ConfigManager: Validate flags unknown default model")
{
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(
default_model = "missing-model"
)");
	REQUIRE(cfg.ok());
	auto st = mgr.Validate(*cfg);
	CHECK_FALSE(st.ok());
	CHECK(st.code() == absl::StatusCode::kFailedPrecondition);
}

TEST_CASE("ConfigManager: Validate passes on consistent config")
{
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(
default_model = "gpt-4o"

[providers.openai]
type = "openai"
api_key = "sk-x"

[models."gpt-4o"]
provider = "openai"
model = "gpt-4o"
)");
	REQUIRE(cfg.ok());
	CHECK(mgr.Validate(*cfg).ok());
}

TEST_CASE("ConfigManager: ConfigPath honors CODEHARNESS_HOME")
{
	ScopedEnv guard("CODEHARNESS_HOME", "Z:/fake/home");
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto path = mgr.ConfigPath();
	REQUIRE(path.ok());
	CHECK(*path == "Z:/fake/home/config.toml");
}

TEST_CASE("ConfigManager: Load returns default config when file missing")
{
	auto tmp = std::filesystem::temp_directory_path() / "codeharness_cfg_missing_test";
	std::error_code ec;
	std::filesystem::remove_all(tmp, ec);
	std::filesystem::create_directories(tmp / "nodir", ec);
	ScopedEnv guard("CODEHARNESS_HOME", (tmp / "nodir" / "deeper").string());
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.Load();
	REQUIRE(cfg.ok());
	CHECK(cfg->defaultModel.empty());
	CHECK(cfg->providers.empty());
	std::filesystem::remove_all(tmp, ec);
}

TEST_CASE("ConfigManager: Save then Load round-trips")
{
	auto tmp = std::filesystem::temp_directory_path() / "codeharness_cfg_rt_test";
	std::error_code ec;
	std::filesystem::remove_all(tmp, ec);
	ScopedEnv guard("CODEHARNESS_HOME", tmp.string());

	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto original = SampleConfig();
	original.providers["openai"].baseUrl = "https://proxy.example.com/v1";
	original.providers["openai"].maxTokens = 4096;
	codeharness::mcp::McpServerConfig server;
	server.name = "filesystem";
	server.command = "mcp-filesystem";
	server.args = {"--root", "."};
	server.env["MCP_TOKEN"] = "secret";
	server.cwd = "tools";
	original.mcpServers.push_back(server);

	auto saveStatus = mgr.Save(original);
	REQUIRE(saveStatus.ok());
	CHECK(std::filesystem::exists(tmp / "config.toml", ec));

	auto loaded = mgr.Load();
	REQUIRE(loaded.ok());
	CHECK(loaded->defaultModel == "gpt-4o");
	REQUIRE(loaded->providers.count("openai") == 1);
	CHECK(loaded->providers["openai"].type == config::ProviderType::OpenAi);
	REQUIRE(loaded->providers["openai"].apiKey.has_value());
	CHECK(*loaded->providers["openai"].apiKey == "sk-test");
	REQUIRE(loaded->providers["openai"].maxTokens.has_value());
	CHECK(*loaded->providers["openai"].maxTokens == 4096);
	REQUIRE(loaded->models.count("gpt-4o") == 1);
	REQUIRE(loaded->mcpServers.size() == 1);
	CHECK(loaded->mcpServers[0].name == "filesystem");
	CHECK(loaded->mcpServers[0].command == "mcp-filesystem");
	REQUIRE(loaded->mcpServers[0].args.size() == 2);
	CHECK(loaded->mcpServers[0].args[0] == "--root");
	CHECK(loaded->mcpServers[0].env["MCP_TOKEN"] == "secret");
	CHECK(loaded->mcpServers[0].cwd == "tools");

	std::filesystem::remove_all(tmp, ec);
}

TEST_CASE("ConfigManager: malformed TOML returns InvalidArgument")
{
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(this is = = not valid toml)");
	CHECK_FALSE(cfg.ok());
	CHECK(cfg.status().code() == absl::StatusCode::kInvalidArgument);
}

TEST_CASE("ConfigManager: parses [skills] table")
{
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(
[skills]
allow_project_skills = false
extra_skill_dirs = ["/opt/shared-skills", "/home/me/skills"]
)");
	REQUIRE(cfg.ok());
	CHECK_FALSE(cfg->skills.allowProjectSkills);
	REQUIRE(cfg->skills.extraSkillDirs.size() == 2);
	CHECK_EQ(cfg->skills.extraSkillDirs[0], "/opt/shared-skills");
	CHECK_EQ(cfg->skills.extraSkillDirs[1], "/home/me/skills");
}

TEST_CASE("ConfigManager: skills config defaults when absent")
{
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(default_model = "x")");
	REQUIRE(cfg.ok());
	CHECK(cfg->skills.allowProjectSkills);
	CHECK(cfg->skills.extraSkillDirs.empty());
}

TEST_CASE("ConfigManager: parses MCP server array")
{
	ScopedEnv guard("CODEHARNESS_MCP_TOKEN", "expanded-token");
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(
[[mcp.servers]]
name = "fs"
command = "node"
args = ["server.js", "--root", "."]
cwd = "tools"
enabled = false

[mcp.servers.env]
MCP_TOKEN = "$CODEHARNESS_MCP_TOKEN"
)");
	REQUIRE(cfg.ok());
	REQUIRE(cfg->mcpServers.size() == 1);
	CHECK(cfg->mcpServers[0].name == "fs");
	CHECK(cfg->mcpServers[0].command == "node");
	REQUIRE(cfg->mcpServers[0].args.size() == 3);
	CHECK(cfg->mcpServers[0].args[1] == "--root");
	CHECK(cfg->mcpServers[0].cwd == "tools");
	CHECK_FALSE(cfg->mcpServers[0].enabled);
	CHECK(cfg->mcpServers[0].env["MCP_TOKEN"] == "expanded-token");
}

TEST_CASE("ConfigManager: parses MCP server table")
{
	host::LocalHost host;
	config::ConfigManager mgr(&host);

	auto cfg = mgr.LoadFromString(R"(
[mcp.servers.github]
command = "github-mcp"
args = ["stdio"]
)");
	REQUIRE(cfg.ok());
	REQUIRE(cfg->mcpServers.size() == 1);
	CHECK(cfg->mcpServers[0].name == "github");
	CHECK(cfg->mcpServers[0].command == "github-mcp");
	REQUIRE(cfg->mcpServers[0].args.size() == 1);
	CHECK(cfg->mcpServers[0].args[0] == "stdio");
	CHECK(cfg->mcpServers[0].enabled);
}
