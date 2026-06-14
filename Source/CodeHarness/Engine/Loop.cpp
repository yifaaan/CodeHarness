#include "Loop.h"

#include <algorithm>
#include <span>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "fmt/format.h"
#include "Llm/ChatProvider.h"
#include "spdlog/spdlog.h"

namespace codeharness::engine
{

	namespace
	{

		void Dispatch(const TurnInput &input, LoopEvent event)
		{
			if (input.dispatchEvent)
			{
				input.dispatchEvent(event);
			}
		}

		ToolResult ExecuteToolCall(ExecutableTool &tool, const llm::ToolCall &tc, const ToolContext &ctx)
		{
			nlohmann::json args;
			if (!tc.arguments.empty())
			{
				try
				{
					args = nlohmann::json::parse(tc.arguments);
				}
				catch (const nlohmann::json::parse_error &e)
				{
					return {.content = fmt::format("invalid tool arguments: {}", e.what()), .isError = true};
				}
			}

			auto resolution = tool.ResolveExecution(args);
			if (!resolution.ok())
			{
				return {.content = std::string(resolution.status().message()), .isError = true};
			}

			auto result = tool.Execute(args, ctx);
			if (!result.ok())
			{
				return {.content = std::string(result.status().message()), .isError = true};
			}

			return std::move(*result);
		}

		ExecutableTool *FindTool(std::vector<ExecutableTool *> &tools, std::string_view name)
		{
			for (auto *t : tools)
			{
				if (t->Name() == name)
					return t;
			}
			return nullptr;
		}

	} // namespace

	TurnResult RunTurn(TurnInput input, const LoopHooks &hooks)
	{
		TurnResult result;
		result.updatedHistory = std::move(input.history);

		std::vector<llm::Tool> toolDefs;
		toolDefs.reserve(input.tools.size());
		for (auto *t : input.tools)
		{
			toolDefs.push_back(t->GetToolDefinition());
		}

		for (int step = 1; step <= input.maxSteps; ++step)
		{
			if (input.stopToken.stop_requested())
			{
				result.stopReason = StopReason::Aborted;
				return result;
			}

			if (hooks.beforeStep)
				hooks.beforeStep(step);

			Dispatch(input, StepStartedEvent{step});

			std::string assistantText;
			std::vector<llm::ToolCall> pendingCalls;
			llm::FinishReason finishReason = llm::FinishReason::Other;
			llm::TokenUsage stepUsage{};

			llm::StreamCallbacks callbacks{
				.onText =
					[&](std::string_view text) {
						assistantText += text;
						Dispatch(input, AssistantDeltaEvent{std::string(text)});
					},
				.onThink = {},
				.onToolCallStart =
					[&](int idx, std::string_view id, std::string_view name) {
						if (idx >= static_cast<int>(pendingCalls.size()))
						{
							pendingCalls.resize(idx + 1);
						}
						pendingCalls[idx].id = std::string(id);
						pendingCalls[idx].name = std::string(name);
					},
				.onToolCallDelta =
					[&](int idx, std::string_view args) {
						if (idx < static_cast<int>(pendingCalls.size()))
						{
							pendingCalls[idx].arguments += args;
						}
					},
				.onFinish =
					[&](llm::FinishReason f, const llm::TokenUsage &u) {
						finishReason = f;
						stepUsage = u;
					},
			};

			auto status =
				input.provider->Generate(input.systemPrompt, std::span<const llm::Tool>(toolDefs), std::span<const llm::Message>(result.updatedHistory), callbacks, input.stopToken);

			if (!status.ok())
			{
				result.stopReason = StopReason::Error;
				result.errorMessage = std::string(status.message());
				Dispatch(input, ErrorEvent{result.errorMessage});
				return result;
			}

			result.totalUsage.output += stepUsage.output;
			result.totalUsage.inputOther += stepUsage.inputOther;
			result.totalUsage.inputCacheRead += stepUsage.inputCacheRead;
			result.totalUsage.inputCacheCreation += stepUsage.inputCacheCreation;

			pendingCalls.erase(std::remove_if(pendingCalls.begin(), pendingCalls.end(), [](const llm::ToolCall &tc) { return tc.name.empty(); }),
							   pendingCalls.end());

			llm::Message assistantMsg;
			assistantMsg.role = llm::Role::Assistant;
			if (!assistantText.empty())
			{
				assistantMsg.content.push_back(llm::TextPart{std::move(assistantText)});
			}
			assistantMsg.toolCalls = pendingCalls;
			result.updatedHistory.push_back(std::move(assistantMsg));

			result.stepsExecuted = step;

			Dispatch(input, StepCompletedEvent{step});
			if (hooks.afterStep)
				hooks.afterStep(step);

			bool hasToolCalls = !pendingCalls.empty();
			if (finishReason != llm::FinishReason::ToolCalls || !hasToolCalls)
			{
				if (hooks.shouldContinueAfterStop)
				{
					std::string reasonStr = finishReason == llm::FinishReason::Truncated   ? "max_tokens"
											: finishReason == llm::FinishReason::Completed ? "end_turn"
																						   : "other";
					if (hooks.shouldContinueAfterStop(reasonStr))
					{
						continue;
					}
				}
				result.stopReason = StopReason::Completed;
				return result;
			}

			ToolContext ctx{.host = input.host, .stopToken = input.stopToken};

			for (const auto &tc : pendingCalls)
			{
				if (input.stopToken.stop_requested())
				{
					result.stopReason = StopReason::Aborted;
					return result;
				}

				nlohmann::json args;
				if (!tc.arguments.empty())
				{
					try
					{
						args = nlohmann::json::parse(tc.arguments);
					}
					catch (...)
					{
						args = nlohmann::json::object();
					}
				}

				Dispatch(input, ToolCallStartedEvent{tc.id, tc.name, args});

				auto *tool = FindTool(input.tools, tc.name);
				ToolResult toolResult;
				if (!tool)
				{
					toolResult.isError = true;
					toolResult.content = fmt::format("tool '{}' not found", tc.name);
					spdlog::warn("tool not found: {}", tc.name);
				}
				else
				{
					toolResult = ExecuteToolCall(*tool, tc, ctx);
				}

				Dispatch(input, ToolResultEvent{tc.id, tc.name, toolResult});

				llm::Message toolMsg;
				toolMsg.role = llm::Role::Tool;
				toolMsg.toolCallId = tc.id;
				toolMsg.content.push_back(llm::TextPart{toolResult.content});
				result.updatedHistory.push_back(std::move(toolMsg));
			}
		}

		result.stopReason = StopReason::MaxSteps;
		return result;
	}

} // namespace codeharness::engine
