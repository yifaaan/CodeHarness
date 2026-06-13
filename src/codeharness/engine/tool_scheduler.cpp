#include "codeharness/engine/tool_scheduler.h"

#include <utility>

#include "absl/status/status.h"
#include "codeharness/core/error.h"
#include "codeharness/core/log.h"
#include "codeharness/tools/tool.h"

namespace codeharness {
namespace engine {

// ---------------------------------------------------------------------------
// ToolCallDeduplicator
// ---------------------------------------------------------------------------

void ToolCallDeduplicator::Record(std::string_view name, const nlohmann::json& args, const ToolResultBlock& result) {
  CacheKey key{std::string{name}, args.dump()};
  cache_[std::move(key)] = result;
}

auto ToolCallDeduplicator::GetCached(std::string_view name, const nlohmann::json& args) const
    -> std::optional<ToolResultBlock> {
  CacheKey key{std::string{name}, args.dump()};
  auto it = cache_.find(key);
  if (it != cache_.end()) {
    return it->second;
  }
  return std::nullopt;
}

auto ToolCallDeduplicator::IsDuplicate(std::string_view name, const nlohmann::json& args) const -> bool {
  return GetCached(name, args).has_value();
}

void ToolCallDeduplicator::Clear() { cache_.clear(); }

// ---------------------------------------------------------------------------
// FindTool
// ---------------------------------------------------------------------------

auto FindTool(const std::vector<const Tool*>& tools, std::string_view name) -> const Tool* {
  for (const auto* tool : tools) {
    if (tool != nullptr && tool->name() == name) {
      return tool;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// RunToolCallBatch
// ---------------------------------------------------------------------------

namespace {

auto MakeErrorResult(const ToolUseBlock& tool_use, std::string message) -> ToolResultBlock {
  return ToolResultBlock{.tool_use_id = tool_use.id, .content = std::move(message), .is_error = true};
}

auto MakeToolResult(const ToolUseBlock& tool_use, const ToolResponse& response) -> ToolResultBlock {
  return ToolResultBlock{.tool_use_id = response.tool_use_id.empty() ? tool_use.id : response.tool_use_id,
                         .content = response.content,
                         .is_error = response.is_error};
}

}  // namespace

auto RunToolCallBatch(const std::vector<ToolUseBlock>& tool_calls, const std::vector<const Tool*>& tools,
                      ToolCallDeduplicator& deduplicator, LoopEventDispatcher& event_dispatcher, LoopHooks* hooks,
                      const CancellationToken& cancellation, std::string_view sender_id)
    -> std::vector<ToolResultBlock> {
  std::vector<ToolResultBlock> results;
  results.reserve(tool_calls.size());

  for (const auto& tool_use : tool_calls) {
    if (cancellation.is_cancelled()) {
      results.push_back(MakeErrorResult(tool_use, "cancelled"));
      continue;
    }

    event_dispatcher.Recorded(LoopToolCallStarted{.id = tool_use.id, .name = tool_use.name});

    // Parse input JSON.
    nlohmann::json parsed_args;
    try {
      parsed_args = nlohmann::json::parse(tool_use.input_json);
    } catch (const nlohmann::json::parse_error&) {
      auto error_result = MakeErrorResult(tool_use, "invalid JSON input for tool: " + tool_use.name);
      event_dispatcher.Recorded(LoopToolResult{
          .tool_use_id = error_result.tool_use_id,
          .content = error_result.content,
          .is_error = error_result.is_error,
      });
      results.push_back(std::move(error_result));
      continue;
    }

    // Check deduplication.
    if (deduplicator.IsDuplicate(tool_use.name, parsed_args)) {
      auto cached = deduplicator.GetCached(tool_use.name, parsed_args);
      if (cached) {
        event_dispatcher.Recorded(LoopToolResult{
            .tool_use_id = cached->tool_use_id,
            .content = cached->content,
            .is_error = cached->is_error,
        });
        results.push_back(*cached);
        continue;
      }
    }

    // Pre-execution hook (permission check).
    if (hooks != nullptr) {
      auto hook_result = hooks->PrepareToolExecution(tool_use);
      if (!hook_result.ok()) {
        auto error_result =
            MakeErrorResult(tool_use, "tool blocked by hook: " + std::string{hook_result.status().message()});
        event_dispatcher.Recorded(LoopToolResult{
            .tool_use_id = error_result.tool_use_id,
            .content = error_result.content,
            .is_error = error_result.is_error,
        });
        results.push_back(std::move(error_result));
        continue;
      }
      if (hook_result->has_value() && hook_result->value().blocked) {
        auto error_result = MakeErrorResult(tool_use, "tool blocked by hook: " + hook_result->value().reason);
        event_dispatcher.Recorded(LoopToolResult{
            .tool_use_id = error_result.tool_use_id,
            .content = error_result.content,
            .is_error = error_result.is_error,
        });
        results.push_back(std::move(error_result));
        continue;
      }
    }

    // Find and execute the tool.
    const Tool* tool = FindTool(tools, tool_use.name);
    if (tool == nullptr) {
      auto error_result = MakeErrorResult(tool_use, "tool not found: " + tool_use.name);
      event_dispatcher.Recorded(LoopToolResult{
          .tool_use_id = error_result.tool_use_id,
          .content = error_result.content,
          .is_error = error_result.is_error,
      });
      results.push_back(std::move(error_result));
      continue;
    }

    // Execute the tool.
    ToolRequest request{
        .id = tool_use.id, .name = tool_use.name, .input_json = tool_use.input_json, .parsed_input = parsed_args};

    ToolContext context;
    context.cwd = std::filesystem::current_path();
    if (!sender_id.empty()) {
      context.sender_id = std::string{sender_id};
    }

    ToolResultBlock tool_result;
    try {
      auto response = tool->execute(request, context);
      if (response.ok()) {
        tool_result = MakeToolResult(tool_use, *response);
      } else {
        tool_result = MakeErrorResult(tool_use, std::string{response.status().message()});
      }
    } catch (const std::exception& error) {
      tool_result =
          MakeErrorResult(tool_use, "tool execution failed with unexpected exception: " + std::string{error.what()});
    }

    // Cache the result.
    deduplicator.Record(tool_use.name, parsed_args, tool_result);

    // Post-execution hook.
    if (hooks != nullptr) {
      auto finalize_status = hooks->FinalizeToolResult(tool_use, tool_result);
      if (!finalize_status.ok()) {
        spdlog::warn("finalizeToolResult hook failed: {}", finalize_status.message());
      }
    }

    // Emit result event.
    event_dispatcher.Recorded(LoopToolResult{
        .tool_use_id = tool_result.tool_use_id,
        .content = tool_result.content,
        .is_error = tool_result.is_error,
    });

    results.push_back(std::move(tool_result));
  }

  return results;
}

}  // namespace engine
}  // namespace codeharness
