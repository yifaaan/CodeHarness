#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "Engine/Tool.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness::tools
{

	// Replace an exact substring in a file. When `replace_all` is false the
	// `old_string` must match exactly once (an error is returned for zero or
	// multiple matches). `old_string` must differ from `new_string`.
	class EditFileTool : public engine::ExecutableTool
	{
	public:
		std::string Name() const override
		{
			return "Edit";
		}
		std::string Description() const override;
		nlohmann::json Parameters() const override;

		absl::StatusOr<engine::ToolExecution> ResolveExecution(const nlohmann::json& args) override;
		absl::StatusOr<engine::ToolResult> Execute(const nlohmann::json& args, const engine::ToolContext& ctx) override;
	};

} // namespace codeharness::tools
