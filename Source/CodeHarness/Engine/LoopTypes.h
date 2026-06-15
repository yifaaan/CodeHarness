#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <stop_token>
#include <string>
#include <variant>
#include <vector>

#include "Llm/Types.h"
#include "Tool.h"

namespace codeharness::host
{
	class Host;
}

namespace codeharness::llm
{
	class ChatProvider;
}

namespace codeharness::permission
{
	class PermissionGate;
}

namespace codeharness::hooks
{
	class HookEngine;
}

namespace codeharness::engine
{

	enum class StopReason
	{
		Completed,
		MaxSteps,
		Aborted,
		Error,
	};

	struct StepStartedEvent
	{
		int step;
	};

	struct StepCompletedEvent
	{
		int step;
	};

	struct AssistantDeltaEvent
	{
		std::string text;
	};

	struct ToolCallStartedEvent
	{
		std::string id;
		std::string name;
		nlohmann::json args;
	};

	struct ToolResultEvent
	{
		std::string id;
		std::string name;
		ToolResult result;
	};

	// Fired immediately before the approval callback is invoked for a mutating
	// tool, so a UI/TUI can surface the prompt. The decision still arrives via
	// the synchronous ApprovalCallback; this event is informational only.
	struct PermissionRequestedEvent
	{
		std::string toolName;
		nlohmann::json args;
		std::string description;
	};

	// Fired after a tool is blocked by the gate, before the (error) ToolResult.
	// Lets observers distinguish permission denials from ordinary tool errors.
	struct PermissionDeniedEvent
	{
		std::string toolName;
		std::string description;
	};

	struct ErrorEvent
	{
		std::string message;
	};

	using LoopEvent = std::variant<StepStartedEvent, StepCompletedEvent, AssistantDeltaEvent, ToolCallStartedEvent, ToolResultEvent, PermissionRequestedEvent, PermissionDeniedEvent, ErrorEvent>;

	using EventDispatcher = std::function<void(const LoopEvent&)>;

	struct TurnInput
	{
		llm::ChatProvider* provider = nullptr;
		std::vector<ExecutableTool*> tools;
		host::Host* host = nullptr;
		std::string systemPrompt;
		std::vector<llm::Message> history;
		EventDispatcher dispatchEvent;
		std::stop_token stopToken;
		int maxSteps = 1000;
		// Optional permission gate. When null, all tools are allowed (preserves
		// the pre-permission loop behavior and keeps existing tests green).
		permission::PermissionGate* permissionGate = nullptr;
		// Optional hook engine for the 5 loop-resident events (PreToolUse block,
		// PostToolUse/PostToolUseFailure/Stop/StopFailure informational). When
		// null, hooks are disabled (back-compat; existing tests stay green).
		hooks::HookEngine* hookEngine = nullptr;
	};

	struct TurnResult
	{
		StopReason stopReason = StopReason::Completed;
		int stepsExecuted = 0;
		llm::TokenUsage totalUsage;
		std::vector<llm::Message> updatedHistory;
		std::string errorMessage;
	};

} // namespace codeharness::engine
