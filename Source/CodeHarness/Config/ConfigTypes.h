#pragma once

#include <string>

namespace codeharness::config
{

	// Default permission policy applied to tool executions that are not matched
	// by an explicit permission rule. Mirrors `default_permission_mode` in
	// config.toml; consumed by the (future) Permission module.
	enum class PermissionMode
	{
		Manual, // ask the user before each side effect (default)
		Auto,	// allow within the session after first approval
		Yolo,	// allow everything without asking
	};

	// Every provider declaration in config.toml has a `type`. Only the
	// OpenAI-compatible family can be constructed today; the rest parse and
	// validate but yield `UnimplementedError` at resolution time. This keeps the
	// schema forward-compatible as more `ChatProvider` ports land.
	enum class ProviderType
	{
		OpenAi,
		OpenAiResponses,
		Anthropic,
		Kimi,
		GoogleGenai,
		Vertexai,
	};

	// Static, resolved view of a model used for capability/limit decisions that
	// do not require a live `ChatProvider` instance (e.g. token-budget math in
	// the Context module).
	struct ResolvedProviderConfig
	{
		ProviderType providerType = ProviderType::OpenAi;
		std::string modelName;
		int maxTokens = 0;
		bool supportsThinking = false;
		bool supportsImages = false;
		bool supportsVideos = false;
	};

} // namespace codeharness::config
