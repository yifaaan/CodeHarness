#include "Tui/EventRouter.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include "Agent/AgentTypes.h"
#include "Engine/LoopTypes.h"
#include "Rpc/RpcTypes.h"
#include "Tui/TuiState.h"

namespace codeharness::tui
{

	EventRouter::EventRouter(std::shared_ptr<TuiState> state)
		: state(std::move(state))
	{
	}

	void EventRouter::Dispatch(const rpc::CoreEvent& event)
	{
		// Visit the AgentEvent variant and dispatch to the right handler
		auto& ev = event.event;

		// Use direct if-else on index to avoid MSVC template auto issues in nested visitors
		size_t idx = ev.index();
		if (idx == 0) // TurnStartedEvent
		{
			OnTurnStarted(std::get<agent::TurnStartedEvent>(ev).turnId);
		}
		else if (idx == 1) // LoopEvent
		{
			auto& loopEv = std::get<agent::LoopEvent>(ev).event;
			size_t lidx = loopEv.index();
			if (lidx == 0) // StepStartedEvent
			{
				// no-op
			}
			else if (lidx == 1) // StepCompletedEvent
			{
				// no-op
			}
			else if (lidx == 2) // AssistantDeltaEvent
			{
				OnAssistantDelta(std::get<engine::AssistantDeltaEvent>(loopEv).text);
			}
			else if (lidx == 3) // ToolCallStartedEvent
			{
				auto& tc = std::get<engine::ToolCallStartedEvent>(loopEv);
				OnToolCallStarted(tc.id, tc.name, tc.args);
			}
			else if (lidx == 4) // ToolResultEvent
			{
				auto& tr = std::get<engine::ToolResultEvent>(loopEv);
				OnToolResult(tr.id, tr.name, tr.result);
			}
			else if (lidx == 5) // PermissionRequestedEvent
			{
				auto& pr = std::get<engine::PermissionRequestedEvent>(loopEv);
				OnPermissionRequested(pr.toolName, pr.args, pr.description);
			}
			else if (lidx == 6) // PermissionDeniedEvent
			{
				auto& pd = std::get<engine::PermissionDeniedEvent>(loopEv);
				OnPermissionDenied(pd.toolName, pd.description);
			}
			else if (lidx == 7) // ErrorEvent
			{
				OnError(std::get<engine::ErrorEvent>(loopEv).message);
			}
		}
		else if (idx == 2) // TurnEndedEvent
		{
			OnTurnEnded(std::get<agent::TurnEndedEvent>(ev).result);
		}
		else if (idx == 3) // StatusChangedEvent
		{
			OnStatusChanged(std::get<agent::StatusChangedEvent>(ev).status);
		}
		else if (idx == 4) // ErrorEvent
		{
			OnError(std::get<agent::ErrorEvent>(ev).message);
		}
		else if (idx == 5) // ContextCompactingEvent
		{
			OnCompacting(std::get<agent::ContextCompactingEvent>(ev).messageCount);
		}
		// idx == 6: SkillInvokedEvent — no-op for now
	}

	// ═══════════════════════════════════════════════════════════════════════
	// Per-event handlers
	// ═══════════════════════════════════════════════════════════════════════

	void EventRouter::OnTurnStarted(std::string_view /*turnId*/)
	{
		std::lock_guard<std::mutex> lk(state->mutex);
		state->streaming = true;
		state->currentAssistantBuffer.clear();
		state->lastError.clear();
		state->lastUsage = {};
	}

	void EventRouter::OnAssistantDelta(std::string_view text)
	{
		std::lock_guard<std::mutex> lk(state->mutex);
		state->currentAssistantBuffer.append(text.data(), text.size());

		// Update the last assistant entry in transcript if it exists and is streaming
		if (!state->transcript.empty() &&
			state->transcript.back().kind == TranscriptEntry::Kind::Assistant &&
			state->transcript.back().streaming)
		{
			state->transcript.back().assistantText = state->currentAssistantBuffer;
		}
	}

	void EventRouter::OnToolCallStarted(std::string_view id, std::string_view name,
										const nlohmann::json& args)
	{
		std::lock_guard<std::mutex> lk(state->mutex);

		// Flush any accumulated assistant text first
		FlushAssistantBuffer();

		// Track the new tool call
		ToolCallState tc;
		tc.id = std::string(id);
		tc.name = std::string(name);
		tc.args = args;
		tc.status = "running";
		state->activeToolCalls[std::string(id)] = std::move(tc);
		state->toolCallCount++;

		// Surface a human-readable activity label for the ActivityIndicator.
		// Prefer the most useful arg field; fall back to the bare tool name.
		std::string preview;
		if (args.is_object())
		{
			for (const char* key : {"path", "file_path", "file", "command", "pattern", "query", "url"})
			{
				if (args.contains(key) && args[key].is_string())
				{
					preview = args[key].get<std::string>();
					break;
				}
			}
		}
		state->currentActivity = preview.empty()
									 ? std::string(name)
									 : fmt::format("{}: {}", name, preview);

		// Add transcript entry
		state->transcript.push_back({
			.kind = TranscriptEntry::Kind::ToolCall,
			.toolCallId = std::string(id),
			.toolRunning = true,
		});
	}

	void EventRouter::OnToolResult(std::string_view id, std::string_view /*name*/,
								   const engine::ToolResult& result)
	{
		std::lock_guard<std::mutex> lk(state->mutex);

		auto it = state->activeToolCalls.find(std::string(id));
		if (it != state->activeToolCalls.end())
		{
			it->second.status = result.isError ? "error" : "done";
			it->second.output = result.content;

			// Move from active to completed
			state->completedToolCalls[std::string(id)] = std::move(it->second);
			state->activeToolCalls.erase(it);
		}

		// Clear the activity label when no active tools remain.
		if (state->activeToolCalls.empty())
		{
			state->currentActivity.clear();
		}

		// Update the transcript entry
		for (auto& entry : state->transcript)
		{
			if (entry.toolCallId == id)
			{
				entry.toolRunning = false;
				break;
			}
		}
	}

	void EventRouter::OnPermissionRequested(std::string_view toolName,
											const nlohmann::json& /*args*/,
											std::string_view /*description*/)
	{
		std::lock_guard<std::mutex> lk(state->mutex);
		state->transcript.push_back({
			.kind = TranscriptEntry::Kind::System,
			.text = std::string(toolName) + " awaits approval...",
		});
	}

	void EventRouter::OnPermissionDenied(std::string_view toolName,
										 std::string_view /*description*/)
	{
		std::lock_guard<std::mutex> lk(state->mutex);
		state->transcript.push_back({
			.kind = TranscriptEntry::Kind::System,
			.text = std::string(toolName) + " denied.",
		});
	}

	void EventRouter::OnTurnEnded(const agent::PromptResult& result)
	{
		std::lock_guard<std::mutex> lk(state->mutex);

		// Finalize streaming
		FlushAssistantBuffer();
		state->streaming = false;
		state->lastUsage = result.usage;

		// Add system message about stop reason
		std::string_view reasonStr;
		switch (result.stopReason)
		{
		case engine::StopReason::Completed:
			reasonStr = "completed";
			break;
		case engine::StopReason::MaxSteps:
			reasonStr = "max steps reached";
			break;
		case engine::StopReason::Aborted:
			reasonStr = "cancelled";
			break;
		case engine::StopReason::Error:
			reasonStr = "error";
			break;
		}
		state->transcript.push_back({
			.kind = TranscriptEntry::Kind::System,
			.text = std::string(reasonStr),
		});
	}

	void EventRouter::OnError(std::string_view message)
	{
		std::lock_guard<std::mutex> lk(state->mutex);
		state->lastError = std::string(message);
		state->streaming = false;
		state->transcript.push_back({
			.kind = TranscriptEntry::Kind::System,
			.text = std::string(message),
		});
	}

	void EventRouter::OnStatusChanged(agent::AgentStatus status)
	{
		std::lock_guard<std::mutex> lk(state->mutex);
		state->agentStatus = status;
	}

	void EventRouter::OnCompacting(int messageCount)
	{
		std::lock_guard<std::mutex> lk(state->mutex);
		state->compacting = true;
		state->compactingCount = messageCount;
		state->transcript.push_back({
			.kind = TranscriptEntry::Kind::System,
			.text = "Compacting context...",
		});
	}

	void EventRouter::FlushAssistantBuffer()
	{
		if (!state->currentAssistantBuffer.empty())
		{
			if (!state->transcript.empty() &&
				state->transcript.back().kind == TranscriptEntry::Kind::Assistant &&
				state->transcript.back().streaming)
			{
				state->transcript.back().assistantText = state->currentAssistantBuffer;
				state->transcript.back().streaming = false;
			}
			else
			{
				state->transcript.push_back({
					.kind = TranscriptEntry::Kind::Assistant,
					.assistantText = state->currentAssistantBuffer,
					.streaming = false,
				});
			}
			state->currentAssistantBuffer.clear();
		}
	}

} // namespace codeharness::tui