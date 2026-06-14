#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "Engine/Tool.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness::tools
{

	// Execute a shell command via the host. Runs in the foreground with a timeout;
	// on timeout or cancellation the process is terminated (SIGTERM, then SIGKILL
	// after a grace period). Stdout and stderr are drained concurrently to avoid
	// pipe deadlock on commands that produce large output.
	class BashTool : public engine::ExecutableTool
	{
	public:
		std::string Name() const override
		{
			return "Bash";
		}
		std::string Description() const override;
		nlohmann::json Parameters() const override;

		absl::StatusOr<engine::ToolExecution> ResolveExecution(const nlohmann::json& args) override;
		absl::StatusOr<engine::ToolResult> Execute(const nlohmann::json& args, const engine::ToolContext& ctx) override;
	};

} // namespace codeharness::tools
