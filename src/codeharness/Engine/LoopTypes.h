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

	struct ErrorEvent
	{
		std::string message;
	};

	using LoopEvent = std::variant<StepStartedEvent, StepCompletedEvent, AssistantDeltaEvent, ToolCallStartedEvent, ToolResultEvent, ErrorEvent>;

	using EventDispatcher = std::function<void(const LoopEvent &)>;

	struct TurnInput
	{
		llm::ChatProvider *provider = nullptr;
		std::vector<ExecutableTool *> tools;
		host::Host *host = nullptr;
		std::string systemPrompt;
		std::vector<llm::Message> history;
		EventDispatcher dispatchEvent;
		std::stop_token stopToken;
		int maxSteps = 1000;
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
