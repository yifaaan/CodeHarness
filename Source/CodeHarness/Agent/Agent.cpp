#include "Agent/Agent.h"

#include <algorithm>
#include <utility>

#include "Context/Compactor.h"
#include "Context/ContextMemory.h"
#include "Context/TokenEstimate.h"
#include "Engine/Loop.h"
#include "Hooks/HookEngine.h"
#include "Hooks/HookTypes.h"
#include "Llm/Capability.h"
#include "Llm/ChatProvider.h"
#include "Permission/PermissionGate.h"
#include "Records/AgentRecords.h"
#include "Records/RecordTypes.h"
#include "Skills/SkillManager.h"
#include "Skills/SkillRegistry.h"
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

		// UserPromptSubmit hook (blocking). Fires before the prompt reaches the
		// loop/LLM. Fail-open: a hook error never blocks the turn.
		if (hookEngine != nullptr)
		{
			hooks::HookContext hctx{
				.event = hooks::HookEvent::UserPromptSubmit,
				.target = {},
				.payload = {{"input", std::string(text)}},
			};
			auto block = hookEngine->TriggerBlock(hooks::HookEvent::UserPromptSubmit, hctx);
			if (block.has_value() && block->action == hooks::HookAction::Block)
			{
				auto reason = block->reason.empty() ? std::string{"blocked by hook"} : block->reason;
				return absl::CancelledError(fmt::format("prompt blocked by hook: {}", reason));
			}
		}

		// Resolve the context-window budget from the model capability on first
		// use, unless the caller overrode it via SetCompactionConfig.
		if (!compactionConfigOverridden && compactionConfig.maxContextTokens == 0)
		{
			auto cap = llm::GetCapability(provider->ModelName());
			compactionConfig.maxContextTokens = cap.maxContextTokens;
		}

		llm::Message userMessage;
		userMessage.role = llm::Role::User;
		userMessage.content.push_back(llm::TextPart{std::string(text)});

		// Between-turn compaction: if history + the incoming prompt would cross
		// the threshold, summarize the prefix and keep the tail verbatim. This
		// runs before turnHistory is built, so the loop sees a normal (shorter)
		// std::vector<llm::Message> — no Loop/provider changes needed.
		if (compactionConfig.maxContextTokens > 0 && !history.Empty())
		{
			int64_t used = history.TokenCount() + context::EstimateTokens(userMessage);
			if (context::ShouldCompact(used, compactionConfig))
			{
				Dispatch(ContextCompactingEvent{static_cast<int>(history.Size())});
				if (hookEngine != nullptr)
				{
					hooks::HookContext pctx{
						.event = hooks::HookEvent::PreCompact,
						.target = {},
						.payload = {{"tokenCount", used}, {"contextSize", static_cast<int64_t>(history.Size())}},
					};
					(void)hookEngine->Trigger(hooks::HookEvent::PreCompact, pctx);
				}
				auto r = context::Compact(provider, history.Messages(), compactionConfig);
				if (!r.ok())
				{
					spdlog::warn("agent: compaction failed: {}", r.status().message());
					// Non-fatal: proceed with the un-compacted history.
				}
				else if (*r)
				{
					auto compacted = context::BuildCompactedHistory(
						(*r)->summary, history.Messages(), compactionConfig.retainTail);
					spdlog::info("agent: compacted {} messages -> {} (est. {} -> {} tokens)",
								 history.Size(), compacted.size(), history.TokenCount(), (*r)->newTokenCount);
					history.ReplaceAll(std::move(compacted));
					if (hookEngine != nullptr)
					{
						hooks::HookContext postctx{
							.event = hooks::HookEvent::PostCompact,
							.target = {},
							.payload = {{"newTokenCount", (*r)->newTokenCount}},
						};
						(void)hookEngine->Trigger(hooks::HookEvent::PostCompact, postctx);
					}
				}
			}
		}

		std::vector<llm::Message> turnHistory = history.Messages();
		turnHistory.push_back(userMessage);

		// Build the effective system prompt: the user's base prompt plus (when
		// skills are wired) the model-invocable skill catalog and any prompt-
		// type skill content queued since the last turn. The pending buffer is
		// drained so a prompt-skill does not leak into subsequent turns.
		//
		// When skills are wired, `baseSystemPrompt` is the immutable base
		// (snapshotted at SetSkillManager / SetSystemPrompt time); otherwise
		// `config.systemPrompt` is used directly.
		std::string effectiveSystemPrompt = (skillRegistry != nullptr && baseSystemPromptCaptured)
												? baseSystemPrompt
												: config.systemPrompt;
		if (skillRegistry != nullptr)
		{
			effectiveSystemPrompt += skillRegistry->RenderSkillIndex();
			if (!pendingSystemSkillContent.empty())
			{
				effectiveSystemPrompt += "\n\n";
				effectiveSystemPrompt += pendingSystemSkillContent;
				pendingSystemSkillContent.clear();
			}
		}

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
			.systemPrompt = effectiveSystemPrompt,
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
			.permissionGate = permissionGate.get(),
			.hookEngine = hookEngine,
		};

		auto turnResult = engine::RunTurn(std::move(input));
		history.ReplaceAll(std::move(turnResult.updatedHistory));

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
		history.Clear();
	}

	void Agent::SetSystemPrompt(std::string systemPrompt)
	{
		config.systemPrompt = std::move(systemPrompt);
		// When skills are wired, config.systemPrompt is rebuilt per turn from
		// this base. Keep them in sync.
		baseSystemPrompt = config.systemPrompt;
		baseSystemPromptCaptured = true;
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
		return history.Messages();
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

	void Agent::SetPermissionMode(config::PermissionMode mode)
	{
		permissionMode = mode;
		// Rebuild the gate, preserving the currently installed callback. If a
		// callback was never set, Manual mode will safely deny mutating tools.
		permissionGate = std::make_unique<permission::PermissionGate>(mode, approvalCallback);
	}

	void Agent::SetApprovalCallback(permission::ApprovalCallback callback)
	{
		approvalCallback = callback;
		// If a gate already exists, swap its callback in-place by rebuilding it
		// with the same mode; this keeps the gate's warned-once state fresh per
		// callback change, which is the safe choice.
		if (permissionMode.has_value())
		{
			permissionGate = std::make_unique<permission::PermissionGate>(*permissionMode, approvalCallback);
		}
	}

	void Agent::SetCompactionConfig(context::CompactionConfig cfg)
	{
		compactionConfig = std::move(cfg);
		compactionConfigOverridden = true;
	}

	void Agent::SetHookEngine(hooks::HookEngine* engine)
	{
		hookEngine = engine;
	}

	void Agent::SetSkillManager(skills::SkillManager* manager)
	{
		skillManager = manager;
		if (skillManager != nullptr)
		{
			skillRegistry = skillManager->GetRegistry();

			// Snapshot the current system prompt as the immutable base. From
			// here on, config.systemPrompt is rebuilt per turn from this base +
			// the skill catalog + any queued prompt-skill content.
			if (!baseSystemPromptCaptured)
			{
				baseSystemPrompt = config.systemPrompt;
				baseSystemPromptCaptured = true;
			}

			// inline skills: rendered content becomes a user message.
			skillManager->SetAppendMessageCallback([this](std::span<const char> content) -> absl::Status {
				llm::Message msg;
				msg.role = llm::Role::User;
				msg.content.push_back(llm::TextPart{std::string(content.data(), content.size())});
				history.Append(msg);

				if (records != nullptr && !records->IsRestoring())
				{
					records::ContextAppendMessageRecord msgRec;
					msgRec.message = msg;
					auto s = records->Log(std::move(msgRec));
					if (!s.ok())
						spdlog::warn("records: failed to log skill message: {}", s.message());
				}

				return absl::OkStatus();
			});

			// prompt skills: rendered content is staged for the next turn's
			// system prompt rather than emitted as a user message.
			skillManager->SetAppendSystemCallback([this](std::span<const char> content) -> absl::Status {
				if (!pendingSystemSkillContent.empty())
					pendingSystemSkillContent += "\n\n";
				pendingSystemSkillContent.append(content.data(), content.size());
				return absl::OkStatus();
			});
		}
	}

	absl::Status Agent::ActivateSkill(std::string_view name, std::string_view args)
	{
		if (skillManager == nullptr)
		{
			return absl::FailedPreconditionError("Agent has no skill manager");
		}

		skills::SkillActivationPayload payload;
		payload.name = std::string(name);
		payload.args = std::string(args);
		payload.origin = skills::SkillOrigin::UserSlash;
		payload.depth = 0;

		auto status = skillManager->Activate(payload);
		if (status.ok())
		{
			Dispatch(SkillInvokedEvent{
				.skillName = std::string(name),
				.args = std::string(args),
				.content = {},
			});
		}
		return status;
	}

	absl::Status Agent::Resume()
	{
		if (records == nullptr)
		{
			return absl::FailedPreconditionError("Agent has no records sink");
		}

		history.Clear();

		auto status = records->Replay([this](const records::AgentRecord& record) -> absl::Status {
			if (auto* msg = std::get_if<records::ContextAppendMessageRecord>(&record))
			{
				// Append through ContextMemory so the cached token count stays
				// consistent for subsequent compaction checks.
				history.Append(msg->message);
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
