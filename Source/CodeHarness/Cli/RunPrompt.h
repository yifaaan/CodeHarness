#pragma once

#include <functional>
#include <istream>
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
	// network. `host` and `http` are non-owning and must outlive Run().
	struct RunDeps
	{
		host::Host* host = nullptr;
		llm::HttpClient* http = nullptr;
		std::istream* input = nullptr; // shell input; defaults to std::cin
		std::function<absl::StatusOr<std::pair<llm::ChatProvider*, std::string>>()> resolveProvider;
	};

	// Execute the requested CLI mode end-to-end. Prompt mode keeps the existing
	// one-shot behavior; shell mode keeps one live session for multiple prompts.
	absl::Status Run(const CliOptions& opts, RunDeps deps = {});

	// Resolve the provider from config.toml. Public so tests can exercise the
	// resolution path directly if desired.
	absl::StatusOr<std::pair<std::unique_ptr<llm::ChatProvider>, std::string>>
	ResolveProviderFromConfig(host::Host* host, llm::HttpClient* http, std::string_view modelOverride);

} // namespace codeharness::cli
