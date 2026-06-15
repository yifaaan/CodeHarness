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
#include "Engine/Tool.h"
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

		// Set the permission mode for this agent. When a mode is set the Agent
		// owns a PermissionGate and threads it into every turn; mutating tools
		// are then gated. Calling this rebuilds the gate, preserving any
		// approval callback previously installed via SetApprovalCallback.
		// Passing nullopt disables gating (allow-all) — the agent's default.
		void SetPermissionMode(config::PermissionMode mode);

		// Install the approval callback used by the gate in Manual mode. No-op
		// when no mode is set. Without a callback, Manual mode denies mutating
		// tools (safe default) until a UI wires a real approval flow.
		void SetApprovalCallback(permission::ApprovalCallback callback);

		// Wire an event-sourcing sink. Non-owning; must outlive the agent.
		// When set, Prompt() records turn.prompt + context.append_message +
		// context.append_loop_event, and Cancel() records turn.cancel.
		void SetRecords(records::AgentRecords* records);

		// Replay every record from the sink into in-memory state.
		// Idempotent if the sink is empty. Returns the sink's status on error.
		absl::Status Resume();

	private:
		absl::StatusOr<std::vector<engine::ExecutableTool*>> BuildLoopTools() const;
		void Dispatch(const AgentEvent& event) const;
		void SetStatus(AgentStatus status);
		std::string NextTurnId();

		llm::ChatProvider* provider = nullptr;
		host::Host* host = nullptr;
		tools::ToolManager* toolManager = nullptr;
		AgentConfig config;
		std::vector<llm::Message> history;
		std::vector<std::string> activeTools;
		AgentStatus status = AgentStatus::Idle;
		EventDispatcher dispatchEvent;
		records::AgentRecords* records = nullptr;

		// Owned permission gate; present only when SetPermissionMode is called.
		// When null, TurnInput.permissionGate stays null and tools run ungated.
		std::unique_ptr<permission::PermissionGate> permissionGate;
		permission::ApprovalCallback approvalCallback;
		std::optional<config::PermissionMode> permissionMode;

		// Valid only while a synchronous turn is active; Cancel() signals this source.
		std::optional<std::stop_source> currentStopSource;
		std::uint64_t nextTurnId = 1;
	};

} // namespace codeharness::agent
