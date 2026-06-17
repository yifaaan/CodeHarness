#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "Agent/AgentTypes.h"
#include "Config/ConfigTypes.h"
#include "Context/Compactor.h"
#include "Context/ContextMemory.h"
#include "Engine/Tool.h"
#include "Hooks/HookEngine.h"
#include "Llm/Types.h"
#include "Permission/PermissionGate.h"
#include "Permission/PermissionTypes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness::host
{
	class Host;
}

namespace codeharness::llm
{
	class ChatProvider;
}

namespace codeharness::tools
{
	class ToolManager;
}

namespace codeharness::records
{
	class AgentRecords;
}

namespace codeharness::skills
{
	class SkillManager;
	class SkillRegistry;
} // namespace codeharness::skills

namespace codeharness::agent
{

	class Agent
	{
	public:
		Agent(llm::ChatProvider* provider, host::Host* host = nullptr, tools::ToolManager* toolManager = nullptr, AgentConfig config = {});

		Agent(const Agent&) = delete;
		Agent& operator=(const Agent&) = delete;

		absl::StatusOr<PromptResult> Prompt(std::string_view text);
		void Cancel();
		void ClearContext();
		void SetSystemPrompt(std::string systemPrompt);
		absl::Status SetActiveTools(std::vector<std::string> tools);

		std::vector<std::string> GetActiveTools() const;
		const std::vector<llm::Message>& GetHistory() const;
		AgentStatus GetStatus() const;
		const AgentConfig& GetConfig() const;

		void SetEventDispatcher(EventDispatcher dispatcher);

		void SetPermissionMode(config::PermissionMode mode);
		void SetApprovalCallback(permission::ApprovalCallback callback);
		void SetCompactionConfig(context::CompactionConfig cfg);
		void SetHookEngine(hooks::HookEngine* engine);
		void SetRecords(records::AgentRecords* records);

		absl::Status Resume();

		// Set a skill manager for skill activation. Non-owning; must outlive the
		// agent. When set, ActivateSkill delegates to the manager which appends
		// the rendered skill content as a user message.
		void SetSkillManager(skills::SkillManager* manager);

		// Activate a skill by name with optional arguments. Returns an error if
		// no SkillManager is set or the skill is not found.
		absl::Status ActivateSkill(std::string_view name, std::string_view args = {});

	private:
		absl::StatusOr<std::vector<engine::ExecutableTool*>> BuildLoopTools() const;
		void Dispatch(const AgentEvent& event) const;
		void SetStatus(AgentStatus status);
		std::string NextTurnId();

		llm::ChatProvider* provider = nullptr;
		host::Host* host = nullptr;
		tools::ToolManager* toolManager = nullptr;
		AgentConfig config;
		context::ContextMemory history;
		context::CompactionConfig compactionConfig;
		bool compactionConfigOverridden = false;
		std::vector<std::string> activeTools;
		AgentStatus status = AgentStatus::Idle;
		EventDispatcher dispatchEvent;
		records::AgentRecords* records = nullptr;

		std::unique_ptr<permission::PermissionGate> permissionGate;
		permission::ApprovalCallback approvalCallback;
		std::optional<config::PermissionMode> permissionMode;

		hooks::HookEngine* hookEngine = nullptr;

		skills::SkillManager* skillManager = nullptr;
		skills::SkillRegistry* skillRegistry = nullptr;
		// The user-supplied system prompt, captured at SetSkillManager time so
		// the per-turn system prompt can be rebuilt from it without accumulating
		// skill content across turns.
		std::string baseSystemPrompt;
		// Rendered `prompt`-type skill content queued for the next turn's system
		// prompt. Drained each turn after it is folded in.
		std::string pendingSystemSkillContent;
		bool baseSystemPromptCaptured = false;

		std::optional<std::stop_source> currentStopSource;
		std::uint64_t nextTurnId = 1;
	};

} // namespace codeharness::agent
