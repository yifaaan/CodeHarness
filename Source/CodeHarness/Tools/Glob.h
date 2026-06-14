#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "Engine/Tool.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness::tools
{

	// Find files by a glob pattern (e.g. "src/**/*.cpp"). Wraps the host glob
	// implementation. Overly broad patterns ("**", "**/*", "*") are rejected.
	class GlobTool : public engine::ExecutableTool
	{
	public:
		std::string Name() const override
		{
			return "Glob";
		}
		std::string Description() const override;
		nlohmann::json Parameters() const override;

		absl::StatusOr<engine::ToolExecution> ResolveExecution(const nlohmann::json& args) override;
		absl::StatusOr<engine::ToolResult> Execute(const nlohmann::json& args, const engine::ToolContext& ctx) override;
	};

} // namespace codeharness::tools
