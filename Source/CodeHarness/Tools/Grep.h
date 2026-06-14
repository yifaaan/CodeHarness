#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "Engine/Tool.h"

namespace codeharness::tools
{

	// Search file contents with a regular expression (RE2 syntax). Enumerates
	// candidate files under `path` (optionally filtered by a `glob`) and reports
	// matching lines, matching files, or per-file match counts.
	class GrepTool : public engine::ExecutableTool
	{
	  public:
		std::string Name() const override
		{
			return "Grep";
		}
		std::string Description() const override;
		nlohmann::json Parameters() const override;

		absl::StatusOr<engine::ToolExecution> ResolveExecution(const nlohmann::json &args) override;
		absl::StatusOr<engine::ToolResult> Execute(const nlohmann::json &args, const engine::ToolContext &ctx) override;
	};

} // namespace codeharness::tools
