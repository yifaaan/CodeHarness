#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <memory>
#include <string>
#include <string_view>

#include "Config/Config.h"
#include "Config/ConfigTypes.h"
#include "Llm/ChatProvider.h"

namespace codeharness::llm
{
	class HttpClient;
}

namespace codeharness::config
{

	// Result of resolving a model alias into a live `ChatProvider`. The
	// `ProviderManager` owns the constructed provider via `unique_ptr`.
	struct ResolvedRuntimeProvider
	{
		std::unique_ptr<llm::ChatProvider> provider;
		std::string modelName;
		std::string providerName;
		ProviderType providerType = ProviderType::OpenAi;
	};

	// Converts a user-facing model alias (e.g. `"gpt-4o"`) into a callable
	// `ChatProvider`. Resolution chain:
	//   model alias -> [models] entry -> [providers] entry -> credentials ->
	//   llm::OpenAiConfig -> OpenAiProvider
	//
	// Only the OpenAI-compatible family (`openai`, `kimi`, `openai_responses`)
	// is constructible today; other types parse and validate but return
	// `UnimplementedError` at resolution time.
	class ProviderManager
	{
	public:
		ProviderManager(KimiConfig config, llm::HttpClient* http);

		// Resolve a model alias to a runtime provider instance.
		absl::StatusOr<ResolvedRuntimeProvider> ResolveForModel(std::string_view modelAlias);

		// Resolve a model alias to its static config (no provider construction).
		// Useful for capability/limit queries that do not need a live HTTP client.
		absl::StatusOr<ResolvedProviderConfig> ResolveConfigForModel(std::string_view modelAlias) const;

	private:
		KimiConfig config;
		llm::HttpClient* http;
	};

} // namespace codeharness::config
