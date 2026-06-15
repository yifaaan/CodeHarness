#pragma once

#include <functional>
#include <memory>
#include <string>

#include "Cli/CliOptions.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness::host
{
	class Host;
}
namespace codeharness::llm
{
	class ChatProvider;
	class HttpClient;
}

namespace codeharness::cli
{

	// Test seam + dependency bundle for Run(). Production builds resolve the
	// provider from config; tests inject a MockChatProvider to avoid the
	// network. `resolveProvider` returns the live provider + resolved model
	// name; returning a non-OK status aborts the run with that error.
	//
	// `host` and `http` are non-owning and must outlive Run().
	struct RunDeps
	{
		host::Host* host = nullptr;
		llm::HttpClient* http = nullptr;
		// Returns the provider to use. If null/empty, Run resolves from config.
		// If set (test injection), config/provider resolution is skipped.
		std::function<absl::StatusOr<std::pair<llm::ChatProvider*, std::string>>()> resolveProvider;
	};

	// Execute one non-interactive prompt end-to-end:
	//   load config → resolve provider → build tools → create Session →
	//   wire event dispatcher (stream text to stdout) → set permission mode →
	//   Agent::Prompt → close session.
	//
	// `deps.resolveProvider` lets tests inject a mock provider; when null, Run
	// resolves the real provider from config.toml via ProviderManager.
	absl::Status Run(const CliOptions& opts, RunDeps deps = {});

	// Resolve the provider from config.toml. Public so tests can exercise the
	// resolution path directly if desired.
	absl::StatusOr<std::pair<std::unique_ptr<llm::ChatProvider>, std::string>>
	ResolveProviderFromConfig(host::Host* host, llm::HttpClient* http, std::string_view modelOverride);

} // namespace codeharness::cli
