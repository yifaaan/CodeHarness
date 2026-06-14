#pragma once

#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "Agent/AgentTypes.h"
#include "Engine/Tool.h"
#include "Llm/Types.h"
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

		// Valid only while a synchronous turn is active; Cancel() signals this source.
		std::optional<std::stop_source> currentStopSource;
		std::uint64_t nextTurnId = 1;
	};

} // namespace codeharness::agent
