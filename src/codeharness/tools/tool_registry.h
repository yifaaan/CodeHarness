#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "codeharness/core/error.h"
#include "codeharness/tools/tool.h"

namespace codeharness {

class ToolRegistry {
 public:
  auto add(std::unique_ptr<Tool> tool) -> void;
  auto execute(const ToolRequest& request, const ToolContext& context) const -> absl::StatusOr<ToolResponse>;
  auto names() const -> std::vector<std::string>;

  auto find(std::string_view name) const -> const Tool*;

 private:
  std::unordered_map<std::string, std::unique_ptr<Tool>> tools_;
};

}  // namespace codeharness
