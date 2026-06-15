#include "ToolScheduler.h"

#include <algorithm>
#include <exception>
#include <future>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "Hooks/HookEngine.h"
#include "Hooks/HookTypes.h"
#include "Permission/PermissionGate.h"
#include "absl/status/statusor.h"
#include "fmt/format.h"
#include "spdlog/spdlog.h"

namespace codeharness::engine
{
	namespace
	{

		void Dispatch(const TurnInput& input, LoopEvent event)
		{
			if (input.dispatchEvent)
			{
				input.dispatchEvent(event);
			}
		}

		ExecutableTool* FindTool(std::span<ExecutableTool* const> tools, std::string_view name)
		{
			for (auto* t : tools)
			{
				if (t->Name() == name)
					return t;
			}
			return nullptr;
		}

		struct PreparedToolCall
		{
			llm::ToolCall call;
			nlohmann::json args;
			ExecutableTool* tool = nullptr;
			ToolExecution execution;
			ToolResult result;
			bool hasResult = false;
		};

		PreparedToolCall ResolveToolCall(const llm::ToolCall& tc, const TurnInput& input)
		{
			PreparedToolCall prepared;
			prepared.call = tc;

			if (!tc.arguments.empty())
			{
				try
				{
					prepared.args = nlohmann::json::parse(tc.arguments);
				}
				catch (const nlohmann::json::parse_error& e)
				{
					prepared.result = {.content = fmt::format("invalid tool arguments: {}", e.what()), .isError = true};
					prepared.hasResult = true;
					prepared.args = nlohmann::json::object();
					return prepared;
				}
			}

			prepared.tool = FindTool(input.tools, tc.name);
			if (!prepared.tool)
			{
				prepared.result = {.content = fmt::format("tool '{}' not found", tc.name), .isError = true};
				prepared.hasResult = true;
				spdlog::warn("tool not found: {}", tc.name);
				return prepared;
			}

			auto resolution = prepared.tool->ResolveExecution(prepared.args);
			if (!resolution.ok())
			{
				prepared.result = {.content = std::string(resolution.status().message()), .isError = true};
				prepared.hasResult = true;
				return prepared;
			}

			prepared.execution = std::move(*resolution);
			return prepared;
		}

		void ApplyPermissionAndPreHook(PreparedToolCall& prepared, const ToolContext& ctx, const TurnInput& input)
		{
			if (prepared.hasResult || prepared.tool == nullptr)
				return;

			if (input.permissionGate != nullptr && prepared.execution.requiresPermission)
			{
				Dispatch(input, PermissionRequestedEvent{prepared.call.name, prepared.args, prepared.execution.description});
				if (!input.permissionGate->ShouldRun(true, prepared.call.name, prepared.args, prepared.execution.description))
				{
					Dispatch(input, PermissionDeniedEvent{prepared.call.name, prepared.execution.description});
					prepared.result = {.content = fmt::format("permission denied for tool '{}'", prepared.call.name), .isError = true};
					prepared.hasResult = true;
					return;
				}
			}

			if (input.hookEngine != nullptr)
			{
				hooks::HookContext hctx{
					.event = hooks::HookEvent::PreToolUse,
					.target = prepared.call.name,
					.payload = {{"toolCall", {{"name", prepared.call.name}, {"args", prepared.args}, {"toolCallId", prepared.call.id}}}},
				};
				auto block = input.hookEngine->TriggerBlock(hooks::HookEvent::PreToolUse, hctx, ctx.stopToken);
				if (block.has_value() && block->action == hooks::HookAction::Block)
				{
					auto reason = block->reason.empty() ? std::string{"blocked by hook"} : block->reason;
					prepared.result = {.content = fmt::format("blocked by hook: {}", reason), .isError = true};
					prepared.hasResult = true;
				}
			}
		}

		ToolResult ExecutePreparedTool(const PreparedToolCall& prepared, const ToolContext& ctx)
		{
			try
			{
				auto result = prepared.tool->Execute(prepared.args, ctx);
				if (!result.ok())
				{
					return {.content = std::string(result.status().message()), .isError = true};
				}
				return std::move(*result);
			}
			catch (const std::exception& e)
			{
				return {.content = fmt::format("tool threw exception: {}", e.what()), .isError = true};
			}
			catch (...)
			{
				return {.content = "tool threw unknown exception", .isError = true};
			}
		}

		void EmitPostToolHook(const ScheduledToolResult& scheduled, const TurnInput& input)
		{
			if (input.hookEngine == nullptr)
				return;

			auto ev = scheduled.result.isError ? hooks::HookEvent::PostToolUseFailure : hooks::HookEvent::PostToolUse;
			hooks::HookContext hctx{
				.event = ev,
				.target = scheduled.call.name,
				.payload = {{"toolCall", {{"name", scheduled.call.name}, {"toolCallId", scheduled.call.id}}},
							{"result", {{"isError", scheduled.result.isError}, {"content", scheduled.result.content}}}},
			};
			(void)input.hookEngine->Trigger(ev, hctx, input.stopToken);
		}

		void EmitResult(const ScheduledToolResult& scheduled, const TurnInput& input)
		{
			Dispatch(input, ToolResultEvent{scheduled.call.id, scheduled.call.name, scheduled.result});
			EmitPostToolHook(scheduled, input);
		}

		ScheduledToolResult ExecuteSerial(PreparedToolCall prepared, const ToolContext& ctx)
		{
			if (!prepared.hasResult)
			{
				prepared.result = ExecutePreparedTool(prepared, ctx);
				prepared.hasResult = true;
			}
			return {.call = std::move(prepared.call), .args = std::move(prepared.args), .result = std::move(prepared.result)};
		}

		int EffectiveMaxConcurrentTools(const ToolSchedulerConfig& config)
		{
			return config.maxConcurrentTools;
		}

	} // namespace

	std::vector<ScheduledToolResult> RunToolCallBatch(
		const std::vector<llm::ToolCall>& toolCalls,
		const ToolContext& ctx,
		const TurnInput& input)
	{
		const int maxConcurrent = EffectiveMaxConcurrentTools(input.toolScheduler);
		std::vector<ScheduledToolResult> results;
		results.reserve(toolCalls.size());

		std::vector<PreparedToolCall> concurrentBatch;
		concurrentBatch.reserve(static_cast<std::size_t>(std::max(1, maxConcurrent)));

		auto flushConcurrent = [&]() {
			if (concurrentBatch.empty())
				return;

			std::vector<std::future<ToolResult>> futures;
			futures.reserve(concurrentBatch.size());
			for (const auto& prepared : concurrentBatch)
			{
				const auto* preparedPtr = &prepared;
				futures.push_back(std::async(std::launch::async, [preparedPtr, &ctx]() {
					return ExecutePreparedTool(*preparedPtr, ctx);
				}));
			}

			for (std::size_t i = 0; i < concurrentBatch.size(); ++i)
			{
				auto result = futures[i].get();
				auto scheduled = ScheduledToolResult{
					.call = std::move(concurrentBatch[i].call),
					.args = std::move(concurrentBatch[i].args),
					.result = std::move(result),
				};
				EmitResult(scheduled, input);
				results.push_back(std::move(scheduled));
			}

			concurrentBatch.clear();
		};

		for (const auto& tc : toolCalls)
		{
			if (input.stopToken.stop_requested())
				break;

			auto prepared = ResolveToolCall(tc, input);
			const bool canRunConcurrently =
				!prepared.hasResult && prepared.execution.canRunConcurrently && maxConcurrent > 1;

			if (!canRunConcurrently)
			{
				flushConcurrent();
			}

			Dispatch(input, ToolCallStartedEvent{prepared.call.id, prepared.call.name, prepared.args});
			ApplyPermissionAndPreHook(prepared, ctx, input);

			if (prepared.hasResult)
			{
				flushConcurrent();
				auto scheduled = ExecuteSerial(std::move(prepared), ctx);
				EmitResult(scheduled, input);
				results.push_back(std::move(scheduled));
				continue;
			}

			if (canRunConcurrently)
			{
				concurrentBatch.push_back(std::move(prepared));
				if (static_cast<int>(concurrentBatch.size()) >= maxConcurrent)
				{
					flushConcurrent();
				}
				continue;
			}

			auto scheduled = ExecuteSerial(std::move(prepared), ctx);
			EmitResult(scheduled, input);
			results.push_back(std::move(scheduled));
		}

		flushConcurrent();
		return results;
	}

} // namespace codeharness::engine
