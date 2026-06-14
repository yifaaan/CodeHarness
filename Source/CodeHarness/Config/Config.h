#pragma once

#include <map>
#include <optional>
#include <string>

#include "Config/ConfigTypes.h"
#include "Llm/Types.h"

namespace codeharness::config
{

	// Per-provider thinking override. Empty when not configured.
	struct ThinkingConfig
	{
		std::optional<llm::ThinkingEffort> effort;
		std::optional<int> budgetTokens;
	};

	// A single `[providers.<name>]` declaration. The `env` sub-table is **not**
	// the process environment — it is an explicit TOML table whose keys happen
	// to use environment-variable naming (e.g. `OPENAI_API_KEY`), used as a
	// secondary credential source.
	struct ProviderConfig
	{
		ProviderType type = ProviderType::OpenAi;
		std::optional<std::string> apiKey;
		std::optional<std::string> baseUrl; // e.g. "https://my-proxy.example.com/v1"
		std::optional<std::string> oauth;	// reference to an OAuth provider (parsed, unused in v1)
		std::map<std::string, std::string> env;
		std::map<std::string, std::string> customHeaders;
		std::optional<int> maxTokens;
		std::optional<ThinkingConfig> thinking;
	};

	// A `[models."alias"]` entry mapping a user-facing model name to the
	// concrete provider + provider-side model identifier.
	struct ModelAlias
	{
		std::string provider; // references `[providers.<name>]`
		std::string model;	  // model name as the provider knows it
	};

	// Root config object produced by `ConfigManager`. Only `general`, the
	// provider table, the model table, and the global thinking block are
	// consumed today; `permission`/`hooks`/`background` fields are intentionally
	// absent and will be added when their modules are ported.
	struct KimiConfig
	{
		std::string defaultModel;
		bool defaultThinking = false;
		PermissionMode defaultPermissionMode = PermissionMode::Manual;
		std::map<std::string, ProviderConfig> providers;
		std::map<std::string, ModelAlias> models;
		std::optional<ThinkingConfig> thinking;
	};

} // namespace codeharness::config
