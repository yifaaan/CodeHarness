#pragma once

#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Agent/AgentTypes.h"
#include "Config/ConfigTypes.h"
#include "Permission/PermissionTypes.h"
#include "Session/SessionTypes.h"
#include "Tools/AskUser.h"
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

	struct CoreSessionInfo
	{
		session::SessionInfo session;
		std::string model;
		config::PermissionMode permissionMode = config::PermissionMode::Manual;
		bool planMode = false;
		bool active = false;
	};

	struct ToolInfo
	{
		std::string name;
		std::string description;
		nlohmann::json inputSchema;
	};

	struct ModelInfo
	{
		std::string alias;
		std::string provider;
		std::string model;
		bool isDefault = false;
	};

	struct McpServerInfo
	{
		std::string name;
		std::string command;
		std::vector<std::string> args;
		std::string cwd;
		bool enabled = true;
		bool connected = false;
	};

	struct BackgroundTaskInfo
	{
		std::string taskId;
		std::string command;
		std::string cwd;
		std::string status;
		int exitCode = -1;
	};

	struct CoreApiConfig
	{
		host::Host* host = nullptr;
		llm::HttpClient* http = nullptr;
		ProviderResolver providerResolver;
		CoreEventSink eventSink;
		permission::ApprovalCallback approvalCallback;
		tools::QuestionCallback questionCallback;
	};

	nlohmann::json PromptResultToJson(const agent::PromptResult& result);
	nlohmann::json CoreEventToJson(const CoreEvent& event);

} // namespace codeharness::rpc
