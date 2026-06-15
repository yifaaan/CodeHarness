#pragma once

#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "Engine/LoopTypes.h"
#include "Llm/Types.h"

namespace codeharness::agent
{

	enum class AgentStatus
	{
		Idle,
		Running,
		Cancelling,
	};

	enum class PromptOrigin
	{
		User,
		SystemTrigger,
	};

	struct AgentProfile
	{
		std::string name = "default";
		std::string systemPrompt;
		std::vector<std::string> tools;
	};

	struct AgentConfig
	{
		std::string systemPrompt;
		int maxSteps = 1000;
		AgentProfile profile;
	};

	struct PromptResult
	{
		std::string turnId;
		engine::StopReason stopReason = engine::StopReason::Completed;
		int stepsExecuted = 0;
		llm::TokenUsage usage;
		std::string errorMessage;
	};

	struct TurnStartedEvent
	{
		std::string turnId;
	};

	struct LoopEvent
	{
		engine::LoopEvent event;
	};

	struct TurnEndedEvent
	{
		PromptResult result;
	};

	struct StatusChangedEvent
	{
		AgentStatus status = AgentStatus::Idle;
	};

	struct ErrorEvent
	{
		std::string message;
	};

	struct ContextCompactingEvent
	{
		int messageCount = 0;
	};

	struct SkillInvokedEvent
	{
		std::string skillName;
		std::string args;
		std::string content;
	};

	using AgentEvent = std::variant<TurnStartedEvent, LoopEvent, TurnEndedEvent, StatusChangedEvent, ErrorEvent, ContextCompactingEvent, SkillInvokedEvent>;
	using EventDispatcher = std::function<void(const AgentEvent&)>;

} // namespace codeharness::agent
