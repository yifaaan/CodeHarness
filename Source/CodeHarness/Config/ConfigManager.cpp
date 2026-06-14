#include "ConfigManager.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <toml++/toml.h>

#include <cstdlib>
#include <regex>
#include <string>
#include <string_view>

#include "Host/Host.h"

namespace codeharness::config
{
	namespace
	{

		// Expand `$VAR` and `${VAR}` references against the process environment.
		// Unset variables expand to an empty string so credential validation
		// downstream flags the missing secret rather than sending a literal
		// `$VAR` token to the provider.
		std::string ExpandEnv(std::string_view in)
		{
			static const std::regex re(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)\}|\$([A-Za-z_][A-Za-z0-9_]*))");
			std::string s(in);
			std::string out;
			std::size_t pos = 0;
			for (std::sregex_iterator it(s.begin(), s.end(), re), end; it != end; ++it)
			{
				const auto& m = *it;
				out.append(s, pos, m.position() - pos);
				std::string var = m[1].matched ? m[1].str() : m[2].str();
				if (const char* val = std::getenv(var.c_str()))
					out += val;
				pos = m.position() + m.length();
			}
			out.append(s, pos, std::string::npos);
			return out;
		}

		ProviderType ParseProviderType(std::string_view s)
		{
			if (s == "openai")
				return ProviderType::OpenAi;
			if (s == "openai_responses")
				return ProviderType::OpenAiResponses;
			if (s == "anthropic")
				return ProviderType::Anthropic;
			if (s == "kimi")
				return ProviderType::Kimi;
			if (s == "google-genai" || s == "google_genai")
				return ProviderType::GoogleGenai;
			if (s == "vertexai")
				return ProviderType::Vertexai;
			SPDLOG_WARN("unknown provider type '{}', defaulting to openai", s);
			return ProviderType::OpenAi;
		}

		std::string_view ProviderTypeName(ProviderType t)
		{
			switch (t)
			{
			case ProviderType::OpenAi:
				return "openai";
			case ProviderType::OpenAiResponses:
				return "openai_responses";
			case ProviderType::Anthropic:
				return "anthropic";
			case ProviderType::Kimi:
				return "kimi";
			case ProviderType::GoogleGenai:
				return "google-genai";
			case ProviderType::Vertexai:
				return "vertexai";
			}
			return "openai";
		}

		PermissionMode ParsePermissionMode(std::string_view s)
		{
			if (s == "auto")
				return PermissionMode::Auto;
			if (s == "yolo")
				return PermissionMode::Yolo;
			if (s == "manual")
				return PermissionMode::Manual;
			SPDLOG_WARN("unknown permission mode '{}', defaulting to manual", s);
			return PermissionMode::Manual;
		}

		std::string_view PermissionModeName(PermissionMode m)
		{
			switch (m)
			{
			case PermissionMode::Manual:
				return "manual";
			case PermissionMode::Auto:
				return "auto";
			case PermissionMode::Yolo:
				return "yolo";
			}
			return "manual";
		}

		std::optional<llm::ThinkingEffort> ParseThinkingEffort(std::string_view s)
		{
			if (s == "off")
				return llm::ThinkingEffort::Off;
			if (s == "low")
				return llm::ThinkingEffort::Low;
			if (s == "medium")
				return llm::ThinkingEffort::Medium;
			if (s == "high")
				return llm::ThinkingEffort::High;
			if (s == "xhigh")
				return llm::ThinkingEffort::XHigh;
			if (s == "max")
				return llm::ThinkingEffort::Max;
			return std::nullopt;
		}

		std::string_view ThinkingEffortName(llm::ThinkingEffort e)
		{
			switch (e)
			{
			case llm::ThinkingEffort::Off:
				return "off";
			case llm::ThinkingEffort::Low:
				return "low";
			case llm::ThinkingEffort::Medium:
				return "medium";
			case llm::ThinkingEffort::High:
				return "high";
			case llm::ThinkingEffort::XHigh:
				return "xhigh";
			case llm::ThinkingEffort::Max:
				return "max";
			}
			return "off";
		}

		// Parse a `[thinking]` or `[providers.<n>.thinking]` table. Returns
		// `nullopt` if neither `effort` nor `budget_tokens` is present. Templated
		// so it accepts both `node_view<node>` and `node_view<const node>`.
		template <typename Node>
		std::optional<ThinkingConfig> ParseThinking(const toml::node_view<Node>& view)
		{
			ThinkingConfig out;
			if (auto effort = view["effort"].value<std::string>())
			{
				out.effort = ParseThinkingEffort(*effort);
				if (!out.effort)
					SPDLOG_WARN("unknown thinking effort '{}', ignored", *effort);
			}
			if (auto budget = view["budget_tokens"].value<int64_t>())
				out.budgetTokens = static_cast<int>(*budget);
			if (!out.effort && !out.budgetTokens)
				return std::nullopt;
			return out;
		}

		std::string TomlEscape(std::string_view s)
		{
			std::string out;
			out.reserve(s.size() + 2);
			for (char c : s)
			{
				switch (c)
				{
				case '\\':
					out += "\\\\";
					break;
				case '"':
					out += "\\\"";
					break;
				case '\n':
					out += "\\n";
					break;
				case '\r':
					out += "\\r";
					break;
				case '\t':
					out += "\\t";
					break;
				default:
					out += c;
				}
			}
			return out;
		}

		void SerializeThinking(std::string& out, std::string_view indent, const std::optional<ThinkingConfig>& t)
		{
			if (!t)
				return;
			out += fmt::format("{}[thinking]\n", indent);
			if (t->effort)
				out += fmt::format("{}effort = \"{}\"\n", indent, ThinkingEffortName(*t->effort));
			if (t->budgetTokens)
				out += fmt::format("{}budget_tokens = {}\n", indent, *t->budgetTokens);
		}

		std::string SerializeConfig(const KimiConfig& config)
		{
			std::string out;
			out += fmt::format("default_model = \"{}\"\n", TomlEscape(config.defaultModel));
			out += fmt::format("default_thinking = {}\n", config.defaultThinking ? "true" : "false");
			out += fmt::format("default_permission_mode = \"{}\"\n", PermissionModeName(config.defaultPermissionMode));

			for (const auto& [name, provider] : config.providers)
			{
				out += fmt::format("\n[providers.{}]\n", TomlEscape(name));
				out += fmt::format("type = \"{}\"\n", ProviderTypeName(provider.type));
				if (provider.apiKey)
					out += fmt::format("api_key = \"{}\"\n", TomlEscape(*provider.apiKey));
				if (provider.baseUrl)
					out += fmt::format("base_url = \"{}\"\n", TomlEscape(*provider.baseUrl));
				if (provider.oauth)
					out += fmt::format("oauth = \"{}\"\n", TomlEscape(*provider.oauth));
				if (provider.maxTokens)
					out += fmt::format("max_tokens = {}\n", *provider.maxTokens);
				for (const auto& [k, v] : provider.customHeaders)
					out += fmt::format("\"{}\" = \"{}\" # custom header\n", TomlEscape(k), TomlEscape(v));
				if (!provider.env.empty())
				{
					out += fmt::format("\n[providers.{}.env]\n", TomlEscape(name));
					for (const auto& [k, v] : provider.env)
						out += fmt::format("{} = \"{}\"\n", TomlEscape(k), TomlEscape(v));
				}
				SerializeThinking(out, "", provider.thinking);
			}

			for (const auto& [alias, entry] : config.models)
			{
				out += fmt::format("\n[models.\"{}\"]\n", TomlEscape(alias));
				out += fmt::format("provider = \"{}\"\n", TomlEscape(entry.provider));
				out += fmt::format("model = \"{}\"\n", TomlEscape(entry.model));
			}

			if (config.thinking)
			{
				out += "\n";
				SerializeThinking(out, "", config.thinking);
			}
			return out;
		}

	} // namespace

	ConfigManager::ConfigManager(host::Host* host) : host(host)
	{
	}

	absl::StatusOr<std::string> ConfigManager::ConfigPath() const
	{
		if (const char* home = std::getenv("CODEHARNESS_HOME"))
		{
			if (home[0] != '\0')
				return fmt::format("{}/config.toml", home);
		}
		auto homeStatus = host->GetHome();
		if (!homeStatus.ok())
			return homeStatus.status();
		return fmt::format("{}/.codeharness/config.toml", *homeStatus);
	}

	absl::StatusOr<KimiConfig> ConfigManager::Load()
	{
		auto path = ConfigPath();
		if (!path.ok())
			return path.status();

		auto text = host->ReadText(*path);
		if (!text.ok())
		{
			if (absl::IsNotFound(text.status()))
				return KimiConfig{};
			return text.status();
		}
		return LoadFromString(*text);
	}

	absl::StatusOr<KimiConfig> ConfigManager::LoadFromString(std::string_view toml) const
	{
		toml::table root;
		try
		{
			root = toml::parse(std::string(toml));
		}
		catch (const toml::parse_error& e)
		{
			return absl::InvalidArgumentError(fmt::format("config parse error: {}", e.description()));
		}

		KimiConfig config;

		if (auto v = root["default_model"].value<std::string>())
			config.defaultModel = *v;
		if (auto v = root["default_thinking"].value<bool>())
			config.defaultThinking = *v;
		if (auto v = root["default_permission_mode"].value<std::string>())
			config.defaultPermissionMode = ParsePermissionMode(*v);

		if (const auto* providers = root["providers"].as_table())
		{
			for (const auto& [key, value] : *providers)
			{
				const auto* ptable = value.as_table();
				if (!ptable)
					continue;

				ProviderConfig pc;
				if (auto t = (*ptable)["type"].value<std::string>())
					pc.type = ParseProviderType(*t);
				if (auto k = (*ptable)["api_key"].value<std::string>())
					pc.apiKey = ExpandEnv(*k);
				if (auto b = (*ptable)["base_url"].value<std::string>())
					pc.baseUrl = *b;
				if (auto o = (*ptable)["oauth"].value<std::string>())
					pc.oauth = *o;
				if (auto m = (*ptable)["max_tokens"].value<int64_t>())
					pc.maxTokens = static_cast<int>(*m);
				pc.thinking = ParseThinking((*ptable)["thinking"]);

				if (const auto* env = (*ptable)["env"].as_table())
				{
					for (const auto& [ek, ev] : *env)
					{
						if (auto val = ev.value<std::string>())
							pc.env[std::string(ek.str())] = ExpandEnv(*val);
					}
				}
				if (const auto* headers = (*ptable)["custom_headers"].as_table())
				{
					for (const auto& [hk, hv] : *headers)
					{
						if (auto val = hv.value<std::string>())
							pc.customHeaders[std::string(hk.str())] = *val;
					}
				}

				config.providers[std::string(key.str())] = std::move(pc);
			}
		}

		if (const auto* models = root["models"].as_table())
		{
			for (const auto& [key, value] : *models)
			{
				const auto* mtable = value.as_table();
				if (!mtable)
					continue;
				ModelAlias alias;
				if (auto p = (*mtable)["provider"].value<std::string>())
					alias.provider = *p;
				if (auto m = (*mtable)["model"].value<std::string>())
					alias.model = *m;
				if (!alias.provider.empty())
					config.models[std::string(key.str())] = std::move(alias);
			}
		}

		config.thinking = ParseThinking(root["thinking"]);

		return config;
	}

	absl::Status ConfigManager::Validate(const KimiConfig& config) const
	{
		if (!config.defaultModel.empty() && config.models.find(config.defaultModel) == config.models.end())
		{
			return absl::FailedPreconditionError(
				fmt::format("default_model '{}' is not defined in [models]", config.defaultModel));
		}

		for (const auto& [alias, entry] : config.models)
		{
			if (config.providers.find(entry.provider) == config.providers.end())
			{
				return absl::FailedPreconditionError(
					fmt::format("model '{}' references unknown provider '{}'", alias, entry.provider));
			}
		}

		for (const auto& [name, provider] : config.providers)
		{
			const bool hasApiKey = provider.apiKey && !provider.apiKey->empty();
			const bool hasEnvKey = provider.env.count("OPENAI_API_KEY") > 0 ||
								   provider.env.count("ANTHROPIC_API_KEY") > 0 ||
								   provider.env.count("MOONSHOT_API_KEY") > 0 ||
								   provider.env.count("GOOGLE_API_KEY") > 0;
			if (!hasApiKey && !hasEnvKey && !provider.oauth)
			{
				SPDLOG_WARN("provider '{}' has no credentials configured", name);
			}
		}

		return absl::OkStatus();
	}

	absl::Status ConfigManager::Save(const KimiConfig& config) const
	{
		auto path = ConfigPath();
		if (!path.ok())
			return path.status();

		const auto slash = path->find_last_of("/\\");
		if (slash != std::string::npos)
		{
			std::string dir = path->substr(0, slash);
			if (auto st = host->Mkdir(dir, {.existOk = true, .recursive = true}); !st.ok())
				return st;
		}
		return host->WriteText(*path, SerializeConfig(config));
	}

} // namespace codeharness::config
