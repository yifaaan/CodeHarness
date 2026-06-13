#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/status/statusor.h"
#include "codeharness/core/cancellation.h"
#include "codeharness/core/message.h"
#include "codeharness/engine/loop_types.h"
#include "codeharness/tools/tool.h"

namespace codeharness {
namespace engine {

// ---------------------------------------------------------------------------
// ToolCallDeduplicator -- caches tool results by (name, args) to avoid
// re-executing identical tool calls within the same turn.
// ---------------------------------------------------------------------------
class ToolCallDeduplicator {
 public:
  // Record a tool result in the cache.
  void Record(std::string_view name, const nlohmann::json& args, const ToolResultBlock& result);

  // Check if a tool call has already been cached.
  // Returns the cached result, or std::nullopt if not found.
  [[nodiscard]] auto GetCached(std::string_view name, const nlohmann::json& args) const
      -> std::optional<ToolResultBlock>;

  // Check if a (name, args) pair is a duplicate.
  [[nodiscard]] auto IsDuplicate(std::string_view name, const nlohmann::json& args) const -> bool;

  // Clear the cache (call at the start of each new turn).
  void Clear();

 private:
  struct CacheKey {
    std::string name;
    std::string args_json;

    bool operator==(const CacheKey& other) const { return name == other.name && args_json == other.args_json; }
  };

  struct CacheKeyHash {
    auto operator()(const CacheKey& key) const -> size_t {
      return std::hash<std::string>{}(key.name) ^ (std::hash<std::string>{}(key.args_json) << 1);
    }
  };

  std::unordered_map<CacheKey, ToolResultBlock, CacheKeyHash> cache_;
};

// ---------------------------------------------------------------------------
// RunToolCallBatch -- execute a batch of tool calls from the LLM
//
// For each tool call:
//   1. Find matching tool in the tools list
//   2. Check deduplication
//   3. Execute via LoopHooks::PrepareToolExecution (permission check)
//   4. Execute the tool
//   5. Emit events via LoopEventDispatcher
//   6. Cache result and finalize via LoopHooks::FinalizeToolResult
// ---------------------------------------------------------------------------
std::vector<ToolResultBlock> RunToolCallBatch(const std::vector<ToolUseBlock>& tool_calls,
                                              const std::vector<const Tool*>& tools, ToolCallDeduplicator& deduplicator,
                                              LoopEventDispatcher& event_dispatcher, LoopHooks* hooks,
                                              const CancellationToken& cancellation, std::string_view sender_id = {});

// ---------------------------------------------------------------------------
// FindTool -- look up a tool by name in the tools vector
// ---------------------------------------------------------------------------
[[nodiscard]] const Tool* FindTool(const std::vector<const Tool*>& tools, std::string_view name);

}  // namespace engine
}  // namespace codeharness
