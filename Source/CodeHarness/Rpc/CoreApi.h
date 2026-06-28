#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Agent/AgentTypes.h"
#include "Llm/Types.h"
#include "Rpc/RpcTypes.h"
#include "Session/SessionTypes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness::llm
{
	class ChatProvider;
}
namespace codeharness::session
{
	class SessionStore;
}

namespace codeharness::rpc
{

	class CoreApi
	{
	public:
		explicit CoreApi(CoreApiConfig config);
		~CoreApi();

		CoreApi(const CoreApi&) = delete;
		CoreApi& operator=(const CoreApi&) = delete;

		absl::StatusOr<std::string> CreateSession(CreateSessionOptions options);
		absl::StatusOr<std::string> ResumeSession(std::string_view sessionId, CreateSessionOptions options = {});
		absl::Status CloseSession(std::string_view sessionId);
		absl::StatusOr<std::vector<session::SessionInfo>> ListSessions(std::string_view workdir = {});
		absl::StatusOr<CoreSessionInfo> GetSessionInfo(std::string_view sessionId);
		absl::Status RenameSession(std::string_view sessionId, std::string_view title);
		absl::Status RemoveSession(std::string_view sessionId);
		absl::StatusOr<std::string> ForkSession(std::string_view sessionId, std::string_view title = {});
		absl::StatusOr<std::string> ExportSessionZip(std::string_view sessionId, std::string_view outputPath = {});

		absl::StatusOr<agent::PromptResult> Prompt(std::string_view sessionId, std::string_view text);
		absl::Status Cancel(std::string_view sessionId);
		absl::Status ClearContext(std::string_view sessionId);
		absl::Status CompactNow(std::string_view sessionId);
		absl::Status SetModel(std::string_view sessionId, std::string_view model);
		absl::Status SetPermissionMode(std::string_view sessionId, config::PermissionMode permissionMode);
		absl::Status SetPlanMode(std::string_view sessionId, bool enabled);
		// Toggle extended thinking on the active session's provider. Only the
		// OpenAI-compatible provider family honors this today; other providers
		// return UnimplementedError. effort == nullopt disables thinking.
		absl::Status SetThinking(std::string_view sessionId, std::optional<llm::ThinkingEffort> effort);
		absl::Status ActivateSkill(std::string_view sessionId, std::string_view name, std::string_view args = {});
		absl::StatusOr<std::vector<ModelInfo>> ListModels();
		absl::StatusOr<std::vector<ToolInfo>> ListTools(std::string_view sessionId);
		absl::StatusOr<std::vector<McpServerInfo>> ListMcpServers();
		absl::StatusOr<std::vector<BackgroundTaskInfo>> ListBackgroundTasks();
		absl::StatusOr<std::string> ReadTaskOutput(std::string_view taskId);
		absl::Status StopTask(std::string_view taskId);

		void SetEventSink(CoreEventSink sink);
		void SetApprovalCallback(permission::ApprovalCallback callback);
		void SetQuestionCallback(tools::QuestionCallback callback);

	private:
		struct CoreSessionRuntime;

		absl::Status EnsureSessionStore();
		absl::StatusOr<std::string> ResolveWorkdir(std::string_view workdir);
		absl::StatusOr<std::unique_ptr<CoreSessionRuntime>> BuildRuntime(std::string_view workdir,
																		 std::string_view modelOverride);
		absl::Status ConfigureAgent(CoreSessionRuntime& runtime, config::PermissionMode permissionMode);
		absl::StatusOr<CoreSessionRuntime*> FindOpenRuntime(std::string_view sessionId);
		absl::StatusOr<session::SessionDir> FindSessionDir(std::string_view sessionId);

		CoreApiConfig config;
		std::unique_ptr<session::SessionStore> sessionStore;
		std::unordered_map<std::string, std::unique_ptr<CoreSessionRuntime>> sessions;
	};

	absl::StatusOr<std::pair<std::unique_ptr<llm::ChatProvider>, std::string>>
	ResolveProviderFromConfig(host::Host* host, llm::HttpClient* http, std::string_view modelOverride);

} // namespace codeharness::rpc
