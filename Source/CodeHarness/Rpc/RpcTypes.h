#pragma once

#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>

#include "Agent/AgentTypes.h"
#include "Config/ConfigTypes.h"
#include "Permission/PermissionTypes.h"
#include "Session/SessionTypes.h"
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

namespace codeharness::rpc
{

	using ProviderResolver =
		std::function<absl::StatusOr<std::pair<llm::ChatProvider*, std::string>>(std::string_view modelOverride)>;

	struct CreateSessionOptions
	{
		std::string workdir;
		std::string title = "session";
		std::string model;
		config::PermissionMode permissionMode = config::PermissionMode::Manual;
	};

	struct CoreEvent
	{
		std::string sessionId;
		std::string agentId = "main";
		agent::AgentEvent event;
	};

	using CoreEventSink = std::function<void(const CoreEvent&)>;

	struct CoreApiConfig
	{
		host::Host* host = nullptr;
		llm::HttpClient* http = nullptr;
		ProviderResolver providerResolver;
		CoreEventSink eventSink;
		permission::ApprovalCallback approvalCallback;
	};

	nlohmann::json PromptResultToJson(const agent::PromptResult& result);
	nlohmann::json CoreEventToJson(const CoreEvent& event);

} // namespace codeharness::rpc
