#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "Engine/Tool.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness::tools
{

	// Read a file from the local filesystem and return its contents with
	// 1-based line numbers. Supports paging via `line_offset` (1-based, negative
	// counts from end) and `n_lines`. Binary files (containing NUL bytes) are
	// rejected.
	class ReadFileTool : public engine::ExecutableTool
	{
	public:
		std::string Name() const override
		{
			return "Read";
		}
		std::string Description() const override;
		nlohmann::json Parameters() const override;

		absl::StatusOr<engine::ToolExecution> ResolveExecution(const nlohmann::json& args) override;
		absl::StatusOr<engine::ToolResult> Execute(const nlohmann::json& args, const engine::ToolContext& ctx) override;
	};

} // namespace codeharness::tools
