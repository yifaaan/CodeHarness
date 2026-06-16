#include "Loop.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Llm/ChatProvider.h"
#include "Hooks/HookEngine.h"
#include "Hooks/HookTypes.h"
#include "ToolScheduler.h"
#include "absl/status/status.h"
#include "fmt/format.h"
#include "spdlog/spdlog.h"

namespace codeharness::engine
{

	namespace
	{
		constexpr int kMaxToolCallIndex = 64;

		void Dispatch(const TurnInput& input, LoopEvent event)
		{
			if (input.dispatchEvent)
			{
				input.dispatchEvent(event);
			}
		}

	} // namespace

	TurnResult RunTurn(TurnInput input, const LoopHooks& hooks)
	{
		TurnResult result;
		result.updatedHistory = std::move(input.history);

		std::vector<llm::Tool> toolDefs;
		toolDefs.reserve(input.tools.size());
		for (auto* t : input.tools)
		{
			toolDefs.push_back(t->GetToolDefinition());
		}
		spdlog::debug("engine: RunTurn start history={} tools={} max_steps={}", result.updatedHistory.size(), toolDefs.size(), input.maxSteps);

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
						if (idx < 0 || idx >= kMaxToolCallIndex)
						{
							Dispatch(input, ErrorEvent{fmt::format("invalid tool call index: {}", idx)});
							return;
						}
						if (idx >= static_cast<int>(pendingCalls.size()))
						{
							pendingCalls.resize(idx + 1);
						}
						pendingCalls[idx].id = std::string(id);
						pendingCalls[idx].name = std::string(name);
					},
				.onToolCallDelta =
					[&](int idx, std::string_view args) {
						if (idx < 0 || idx >= kMaxToolCallIndex)
						{
							Dispatch(input, ErrorEvent{fmt::format("invalid tool call index: {}", idx)});
							return;
						}
						if (idx < static_cast<int>(pendingCalls.size()))
						{
							pendingCalls[idx].arguments += args;
						}
					},
				.onFinish =
					[&](llm::FinishReason f, const llm::TokenUsage& u) {
						finishReason = f;
						stepUsage = u;
					},
			};

			spdlog::debug("engine: step {} Generate start history={} tools={} system_prompt_len={}",
						  step, result.updatedHistory.size(), toolDefs.size(), input.systemPrompt.size());
			auto status = absl::OkStatus();
			try
			{
				status = input.provider->Generate(input.systemPrompt, toolDefs, result.updatedHistory, callbacks, input.stopToken);
			}
			catch (const std::bad_alloc&)
			{
				result.stopReason = StopReason::Error;
				result.errorMessage = "bad allocation during provider Generate";
				spdlog::error("engine: step {} bad_alloc during Generate history={} tools={}", step, result.updatedHistory.size(), toolDefs.size());
				Dispatch(input, ErrorEvent{result.errorMessage});
				return result;
			}
			catch (const std::exception& e)
			{
				result.stopReason = StopReason::Error;
				result.errorMessage = fmt::format("provider Generate threw: {}", e.what());
				spdlog::error("engine: step {} Generate threw: {}", step, e.what());
				Dispatch(input, ErrorEvent{result.errorMessage});
				return result;
			}
			spdlog::debug("engine: step {} Generate end ok={} assistant_len={} pending_calls={}",
						  step, status.ok(), assistantText.size(), pendingCalls.size());

			if (!status.ok())
			{
				result.stopReason = StopReason::Error;
				result.errorMessage = std::string(status.message());
				Dispatch(input, ErrorEvent{result.errorMessage});
				if (input.hookEngine != nullptr)
				{
					hooks::HookContext hctx{
						.event = hooks::HookEvent::StopFailure,
						.target = {},
						.payload = {{"error", result.errorMessage}},
					};
					(void)input.hookEngine->Trigger(hooks::HookEvent::StopFailure, hctx, input.stopToken);
				}
				return result;
			}

			result.totalUsage.output += stepUsage.output;
			result.totalUsage.inputOther += stepUsage.inputOther;
			result.totalUsage.inputCacheRead += stepUsage.inputCacheRead;
			result.totalUsage.inputCacheCreation += stepUsage.inputCacheCreation;

			pendingCalls.erase(std::remove_if(pendingCalls.begin(), pendingCalls.end(), [](const llm::ToolCall& tc) { return tc.name.empty(); }),
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
				if (input.hookEngine != nullptr)
				{
					hooks::HookContext hctx{
						.event = hooks::HookEvent::Stop,
						.target = {},
						.payload = {{"steps", result.stepsExecuted}},
					};
					(void)input.hookEngine->Trigger(hooks::HookEvent::Stop, hctx, input.stopToken);
				}
				return result;
			}

			ToolContext ctx{.host = input.host, .stopToken = input.stopToken};
			auto toolResults = RunToolCallBatch(pendingCalls, ctx, input);
			for (auto& scheduled : toolResults)
			{
				if (input.stopToken.stop_requested())
				{
					result.stopReason = StopReason::Aborted;
					return result;
				}

				llm::Message toolMsg;
				toolMsg.role = llm::Role::Tool;
				toolMsg.toolCallId = scheduled.call.id;
				toolMsg.content.push_back(llm::TextPart{scheduled.result.content});
				result.updatedHistory.push_back(std::move(toolMsg));
			}
		}

		result.stopReason = StopReason::MaxSteps;
		return result;
	}

} // namespace codeharness::engine
