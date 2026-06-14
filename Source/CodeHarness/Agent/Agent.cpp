#include "Agent/Agent.h"

#include <algorithm>
#include <utility>

#include "Engine/Loop.h"
#include "Llm/ChatProvider.h"
#include "Records/AgentRecords.h"
#include "Records/RecordTypes.h"
#include "Tools/ToolManager.h"
#include "absl/status/status.h"
#include "fmt/format.h"
#include "spdlog/spdlog.h"

namespace codeharness::agent
{

	Agent::Agent(llm::ChatProvider* provider, host::Host* host, tools::ToolManager* toolManager, AgentConfig config)
		: provider(provider), host(host), toolManager(toolManager), config(std::move(config))
	{
		if (!this->config.profile.systemPrompt.empty() && this->config.systemPrompt.empty())
		{
			this->config.systemPrompt = this->config.profile.systemPrompt;
		}

		if (!this->config.profile.tools.empty())
		{
			activeTools = this->config.profile.tools;
		}
	}

	absl::StatusOr<PromptResult> Agent::Prompt(std::string_view text)
	{
		if (status != AgentStatus::Idle)
		{
			return absl::FailedPreconditionError("agent already has an active turn");
		}

		if (provider == nullptr)
		{
			return absl::FailedPreconditionError("agent requires a chat provider");
		}

		auto loopTools = BuildLoopTools();
		if (!loopTools.ok())
		{
			return loopTools.status();
		}

		std::vector<llm::Message> turnHistory = history;
		llm::Message userMessage;
		userMessage.role = llm::Role::User;
		userMessage.content.push_back(llm::TextPart{std::string(text)});
		turnHistory.push_back(userMessage);

		auto turnId = NextTurnId();
		currentStopSource.emplace();
		SetStatus(AgentStatus::Running);
		Dispatch(TurnStartedEvent{turnId});

		if (records != nullptr && !records->IsRestoring())
		{
			records::TurnPromptRecord promptRec;
			promptRec.turnId = turnId;
			promptRec.input = userMessage.content;
			promptRec.origin = static_cast<int>(PromptOrigin::User);
			auto s1 = records->Log(std::move(promptRec));
			if (!s1.ok())
				spdlog::warn("records: failed to log turn.prompt: {}", s1.message());

			records::ContextAppendMessageRecord msgRec;
			msgRec.message = userMessage;
			auto s2 = records->Log(std::move(msgRec));
			if (!s2.ok())
				spdlog::warn("records: failed to log context.append_message: {}", s2.message());
		}

		engine::TurnInput input{
			.provider = provider,
			.tools = std::move(*loopTools),
			.host = host,
			.systemPrompt = config.systemPrompt,
			.history = std::move(turnHistory),
			.dispatchEvent = [this](const engine::LoopEvent& event) {
				if (records != nullptr && !records->IsRestoring())
				{
					records::ContextAppendLoopEventRecord loopRec;
					loopRec.event = event;
					auto s = records->Log(std::move(loopRec));
					if (!s.ok())
						spdlog::warn("records: failed to log context.append_loop_event: {}", s.message());
				}
				Dispatch(LoopEvent{event});
			},
			.stopToken = currentStopSource->get_token(),
			.maxSteps = config.maxSteps,
		};

		auto turnResult = engine::RunTurn(std::move(input));
		history = std::move(turnResult.updatedHistory);

		PromptResult result{
			.turnId = turnId,
			.stopReason = turnResult.stopReason,
			.stepsExecuted = turnResult.stepsExecuted,
			.usage = turnResult.totalUsage,
			.errorMessage = turnResult.errorMessage,
		};

		if (turnResult.stopReason == engine::StopReason::Error && !turnResult.errorMessage.empty())
		{
			Dispatch(ErrorEvent{turnResult.errorMessage});
		}

		Dispatch(TurnEndedEvent{result});
		currentStopSource.reset();
		SetStatus(AgentStatus::Idle);

		return result;
	}

	void Agent::Cancel()
	{
		if (currentStopSource.has_value() && status != AgentStatus::Idle)
		{
			SetStatus(AgentStatus::Cancelling);
			currentStopSource->request_stop();

			if (records != nullptr && !records->IsRestoring())
			{
				// Best-effort: log a cancel marker. turnId is not tracked outside
				// Prompt(), so emit an empty id — Session layer will enrich later.
				records::TurnCancelRecord rec;
				auto s = records->Log(std::move(rec));
				if (!s.ok())
					spdlog::warn("records: failed to log turn.cancel: {}", s.message());
			}
		}
	}

	void Agent::ClearContext()
	{
		history.clear();
	}

	void Agent::SetSystemPrompt(std::string systemPrompt)
	{
		config.systemPrompt = std::move(systemPrompt);
	}

	absl::Status Agent::SetActiveTools(std::vector<std::string> tools)
	{
		if (toolManager == nullptr && !tools.empty())
		{
			return absl::FailedPreconditionError("agent has no tool manager");
		}

		for (const auto& name : tools)
		{
			if (name.empty())
			{
				return absl::InvalidArgumentError("active tool name cannot be empty");
			}
			if (toolManager->Find(name) == nullptr)
			{
				return absl::InvalidArgumentError(fmt::format("unknown tool '{}'", name));
			}
		}

		activeTools = std::move(tools);
		return absl::OkStatus();
	}

	std::vector<std::string> Agent::GetActiveTools() const
	{
		return activeTools;
	}

	const std::vector<llm::Message>& Agent::GetHistory() const
	{
		return history;
	}

	AgentStatus Agent::GetStatus() const
	{
		return status;
	}

	const AgentConfig& Agent::GetConfig() const
	{
		return config;
	}

	void Agent::SetEventDispatcher(EventDispatcher dispatcher)
	{
		dispatchEvent = std::move(dispatcher);
	}

	void Agent::SetRecords(records::AgentRecords* r)
	{
		records = r;
	}

	absl::Status Agent::Resume()
	{
		if (records == nullptr)
		{
			return absl::FailedPreconditionError("Agent has no records sink");
		}

		history.clear();

		auto status = records->Replay([this](const records::AgentRecord& record) -> absl::Status {
			if (auto* msg = std::get_if<records::ContextAppendMessageRecord>(&record))
			{
				history.push_back(msg->message);
				return absl::OkStatus();
			}
			// turn.prompt / turn.cancel / context.append_loop_event are
			// stateless for the in-memory agent core: they don't mutate
			// history directly. Session layer may interpret them.
			return absl::OkStatus();
		});

		if (!status.ok())
		{
			spdlog::warn("Agent::Resume: replay aborted: {}", status.message());
		}

		return status;
	}

	absl::StatusOr<std::vector<engine::ExecutableTool*>> Agent::BuildLoopTools() const
	{
		if (toolManager == nullptr)
		{
			return std::vector<engine::ExecutableTool*>{};
		}

		if (activeTools.empty())
		{
			return toolManager->LoopTools();
		}

		std::vector<engine::ExecutableTool*> result;
		result.reserve(activeTools.size());
		for (const auto& name : activeTools)
		{
			auto* tool = toolManager->Find(name);
			if (tool == nullptr)
			{
				return absl::FailedPreconditionError(fmt::format("active tool '{}' is no longer registered", name));
			}
			result.push_back(tool);
		}
		return result;
	}

	void Agent::Dispatch(const AgentEvent& event) const
	{
		if (dispatchEvent)
		{
			dispatchEvent(event);
		}
	}

	void Agent::SetStatus(AgentStatus newStatus)
	{
		if (status == newStatus)
		{
			return;
		}

		status = newStatus;
		Dispatch(StatusChangedEvent{status});
	}

	std::string Agent::NextTurnId()
	{
		return fmt::format("turn_{}", nextTurnId++);
	}

} // namespace codeharness::agent
