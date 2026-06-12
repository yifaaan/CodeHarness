#include "codeharness/tools/sleep_tool.h"

#include <fmt/format.h>

#include <chrono>
#include <nlohmann/json.hpp>
#include <thread>

#include "codeharness/core/assign.h"
#include "codeharness/core/error.h"
#include "codeharness/core/json_parse.h"

namespace codeharness {

namespace {

struct SleepInput {
  int seconds = 1;
};

auto parse_sleep_input(const nlohmann::json& input) -> absl::StatusOr<SleepInput> {
  SleepInput parsed;

  if (auto r =
          Assign(parsed.seconds, ReadJsonField<int, JsonFieldMode::kOptionalWithDefault>(input, "seconds", "sleep", 1));
      !r.ok()) {
    return r;
  }

  if (parsed.seconds < 0) {
    return absl::InvalidArgumentError("sleep seconds must be non-negative");
  }

  if (parsed.seconds > 300) {
    return absl::InvalidArgumentError("sleep seconds must not exceed 300");
  }

  return parsed;
}

}  // namespace

auto SleepTool::name() const -> std::string { return "sleep"; }

auto SleepTool::description() const -> std::string {
  return "Pause execution for a specified number of seconds. "
         "Useful for rate-limiting or waiting between operations. Max 300 seconds.";
}

auto SleepTool::is_read_only() const noexcept -> bool { return true; }

auto SleepTool::execute(const ToolRequest& request, const ToolContext& /*context*/) const
    -> absl::StatusOr<ToolResponse> {
  auto parsed = parse_sleep_input(request.parsed_input);
  if (!parsed.ok()) {
    return parsed.status();
  }

  auto duration = std::chrono::seconds(parsed->seconds);
  std::this_thread::sleep_for(duration);

  return ToolResponse{
      .tool_use_id = request.id,
      .content = fmt::format("Slept for {} seconds", parsed->seconds),
      .is_error = false,
  };
}

}  // namespace codeharness
