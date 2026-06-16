#pragma once

#include <memory>

#include "Tui/TuiState.h"
#include "Rpc/RpcTypes.h"

namespace codeharness::tui
{

/// Routes AgentEvent / CoreEvent from the worker thread into TuiState mutations.
/// All public methods are thread-safe (lock state->mutex internally).
class EventRouter
{
public:
	explicit EventRouter(std::shared_ptr<TuiState> state);

	/// Dispatch a CoreEvent from the worker thread.
	/// Locks state, mutates, and returns a flag indicating whether a re-render
	/// was requested (the caller should call ->PostRender).
	void Dispatch(const rpc::CoreEvent& event);

	/// Called when a new turn starts. Clears transient state.
	void OnTurnStarted(std::string_view turnId);

	/// Called when an assistant text delta arrives.
	void OnAssistantDelta(std::string_view text);

	/// Called when a tool call starts.
	void OnToolCallStarted(std::string_view id, std::string_view name, const nlohmann::json& args);

	/// Called when a tool call completes.
	void OnToolResult(std::string_view id, std::string_view name, const engine::ToolResult& result);

	/// Called when permission is requested.
	void OnPermissionRequested(std::string_view toolName, const nlohmann::json& args, std::string_view description);

	/// Called when permission is denied.
	void OnPermissionDenied(std::string_view toolName, std::string_view description);

	/// Called when a turn ends.
	void OnTurnEnded(const agent::PromptResult& result);

	/// Called when an error occurs.
	void OnError(std::string_view message);

	/// Called when agent status changes.
	void OnStatusChanged(agent::AgentStatus status);

	/// Called when compaction is in progress.
	void OnCompacting(int messageCount);

	/// Internal: push a completed message entry to the transcript.
	void FlushAssistantBuffer();

private:
	std::shared_ptr<TuiState> state;
};

} // namespace codeharness::tui