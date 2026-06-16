#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Agent/AgentTypes.h"
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

		absl::StatusOr<agent::PromptResult> Prompt(std::string_view sessionId, std::string_view text);
		absl::Status Cancel(std::string_view sessionId);
		absl::Status ClearContext(std::string_view sessionId);
		absl::Status ActivateSkill(std::string_view sessionId, std::string_view name, std::string_view args = {});

	private:
		struct CoreSessionRuntime;

		absl::Status EnsureSessionStore();
		absl::StatusOr<std::string> ResolveWorkdir(std::string_view workdir);
		absl::StatusOr<std::unique_ptr<CoreSessionRuntime>> BuildRuntime(std::string_view workdir,
																		 std::string_view modelOverride);
		absl::Status ConfigureAgent(CoreSessionRuntime& runtime, config::PermissionMode permissionMode);
		absl::StatusOr<CoreSessionRuntime*> FindOpenRuntime(std::string_view sessionId);

		CoreApiConfig config;
		std::unique_ptr<session::SessionStore> sessionStore;
		std::unordered_map<std::string, std::unique_ptr<CoreSessionRuntime>> sessions;
	};

	absl::StatusOr<std::pair<std::unique_ptr<llm::ChatProvider>, std::string>>
	ResolveProviderFromConfig(host::Host* host, llm::HttpClient* http, std::string_view modelOverride);

} // namespace codeharness::rpc
