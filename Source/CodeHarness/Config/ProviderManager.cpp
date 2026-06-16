#include "ProviderManager.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <fmt/format.h>

#include <optional>
#include <string>
#include <utility>

#include "Llm/Capability.h"
#include "Llm/OpenAiProvider.h"

namespace codeharness::config
{
	namespace
	{

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

		bool IsOpenAiCompatible(ProviderType t)
		{
			return t == ProviderType::OpenAi || t == ProviderType::Kimi || t == ProviderType::OpenAiResponses;
		}

		struct BaseUrlParts
		{
			std::string host = "api.openai.com";
			std::string port = "443";
			std::string path = "/v1/chat/completions";
			bool useTls = true;
		};

		// Split a `base_url` like "https://host[:port]/v1" into the host, port,
		// TLS mode, and full completions path. Missing prefix defaults to `/v1`;
		// the suffix `/chat/completions` is always appended.
		BaseUrlParts SplitBaseUrl(const std::optional<std::string>& baseUrl)
		{
			if (!baseUrl || baseUrl->empty())
				return {};

			BaseUrlParts out;
			std::string url = *baseUrl;
			if (auto pos = url.find("://"); pos != std::string::npos)
			{
				auto scheme = url.substr(0, pos);
				out.useTls = scheme != "http";
				out.port = out.useTls ? "443" : "80";
				url = url.substr(pos + 3);
			}

			std::string prefix;
			if (auto pos = url.find('/'); pos != std::string::npos)
			{
				out.host = url.substr(0, pos);
				prefix = url.substr(pos);
			}
			else
			{
				out.host = url;
			}

			if (auto pos = out.host.rfind(':'); pos != std::string::npos && out.host.find(']') == std::string::npos)
			{
				out.port = out.host.substr(pos + 1);
				out.host = out.host.substr(0, pos);
			}

			while (prefix.size() > 1 && prefix.back() == '/')
				prefix.pop_back();
			if (prefix.empty())
				prefix = "/v1";

			out.path = prefix + "/chat/completions";
			return out;
		}

		// Resolve the API key for a provider from explicit config sources. The
		// `env` sub-table is checked for the well-known credential keys; if none
		// is present, returns empty so `OpenAiProvider` can still apply its own
		// `OPENAI_API_KEY` process-env fallback.
		std::string ResolveApiKey(const ProviderConfig& pc)
		{
			if (pc.apiKey && !pc.apiKey->empty())
				return *pc.apiKey;

			static const char* knownKeys[] = {
				"OPENAI_API_KEY",
				"ANTHROPIC_API_KEY",
				"MOONSHOT_API_KEY",
				"GOOGLE_API_KEY",
				"GEMINI_API_KEY",
			};
			for (const char* k : knownKeys)
			{
				auto it = pc.env.find(k);
				if (it != pc.env.end() && !it->second.empty())
					return it->second;
			}
			for (const auto& [k, v] : pc.env)
			{
				if (k.find("API_KEY") != std::string::npos && !v.empty())
					return v;
			}
			return {};
		}

	} // namespace

	ProviderManager::ProviderManager(KimiConfig config, llm::HttpClient* http)
		: config(std::move(config)), http(http)
	{
	}

	absl::StatusOr<ResolvedProviderConfig> ProviderManager::ResolveConfigForModel(std::string_view modelAlias) const
	{
		auto mit = config.models.find(std::string(modelAlias));
		if (mit == config.models.end())
			return absl::NotFoundError(fmt::format("model '{}' is not defined in [models]", modelAlias));

		auto pit = config.providers.find(mit->second.provider);
		if (pit == config.providers.end())
			return absl::FailedPreconditionError(
				fmt::format("model '{}' references unknown provider '{}'", modelAlias, mit->second.provider));

		const auto& pc = pit->second;

		ResolvedProviderConfig out;
		out.providerType = pc.type;
		out.modelName = mit->second.model;
		if (pc.maxTokens)
			out.maxTokens = *pc.maxTokens;

		const auto cap = llm::GetCapability(mit->second.model);
		out.supportsImages = cap.imageIn;
		out.supportsVideos = cap.videoIn;
		if (out.maxTokens == 0)
			out.maxTokens = static_cast<int>(cap.maxContextTokens);

		if (pc.thinking && pc.thinking->effort)
			out.supportsThinking = *pc.thinking->effort != llm::ThinkingEffort::Off;
		else
			out.supportsThinking = cap.thinking;

		return out;
	}

	absl::StatusOr<ResolvedRuntimeProvider> ProviderManager::ResolveForModel(std::string_view modelAlias)
	{
		auto resolved = ResolveConfigForModel(modelAlias);
		if (!resolved.ok())
			return resolved.status();

		auto mit = config.models.find(std::string(modelAlias));
		auto pit = config.providers.find(mit->second.provider);
		const auto& pc = pit->second;

		if (!IsOpenAiCompatible(pc.type))
		{
			return absl::UnimplementedError(
				fmt::format("provider type '{}' is not yet ported; only openai/kimi/openai_responses are constructible",
							ProviderTypeName(pc.type)));
		}

		llm::OpenAiConfig oc;
		oc.apiKey = ResolveApiKey(pc);
		oc.model = mit->second.model;

		auto baseUrl = SplitBaseUrl(pc.baseUrl);
		oc.host = std::move(baseUrl.host);
		oc.port = std::move(baseUrl.port);
		oc.path = std::move(baseUrl.path);
		oc.useTls = baseUrl.useTls;

		// Provider-specific thinking overrides the global block.
		const std::optional<ThinkingConfig>& thinking = pc.thinking ? pc.thinking : config.thinking;
		if (thinking && thinking->effort)
			oc.thinking = thinking->effort;

		if (pc.maxTokens)
			oc.maxCompletionTokens = *pc.maxTokens;

		auto provider = std::make_unique<llm::OpenAiProvider>(std::move(oc), http);

		ResolvedRuntimeProvider out;
		out.provider = std::move(provider);
		out.modelName = mit->second.model;
		out.providerName = mit->second.provider;
		out.providerType = pc.type;
		return out;
	}

} // namespace codeharness::config
