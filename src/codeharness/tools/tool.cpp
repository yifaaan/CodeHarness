#include "codeharness/tools/tool.h"

#include <string_view>

#include "codeharness/core/error.h"

namespace codeharness {

auto parse_tool_request_input(ToolRequest& request, std::string_view tool_name) -> absl::StatusOr<nlohmann::json*> {
  if (!request.parsed_input.is_null()) {
    return &request.parsed_input;
  }

  try {
    request.parsed_input = nlohmann::json::parse(request.input_json);
  } catch (const nlohmann::json::parse_error& error) {
    return absl::InvalidArgumentError(std::string{tool_name} + " input is not valid JSON: " + error.what());
  }

  return &request.parsed_input;
}

}  // namespace codeharness
