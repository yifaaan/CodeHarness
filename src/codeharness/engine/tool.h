#pragma once

#include <stop_token>
#include <string>

#include <nlohmann/json.hpp>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "llm/types.h"

namespace codeharness::host {
class Host;
}

namespace codeharness::engine {

struct ToolResult {
  std::string content;
  bool is_error = false;
};

struct ToolContext {
  host::Host* host = nullptr;
  std::stop_token stop_token;
};

struct ToolExecution {
  std::string description;
  bool requires_permission = false;
};

class ExecutableTool {
 public:
  virtual ~ExecutableTool() = default;

  virtual std::string Name() const = 0;
  virtual std::string Description() const = 0;
  virtual nlohmann::json Parameters() const = 0;

  virtual absl::StatusOr<ToolExecution> ResolveExecution(const nlohmann::json& args) = 0;
  virtual absl::StatusOr<ToolResult> Execute(const nlohmann::json& args, const ToolContext& ctx) = 0;

  llm::Tool GetToolDefinition() const {
    return {Name(), Description(), Parameters().is_null() ? nlohmann::json::object() : Parameters()};
  }
};

}  // namespace codeharness::engine
