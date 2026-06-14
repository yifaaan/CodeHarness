#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include "Engine/Tool.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness::tools
{

	// Create or overwrite a file. With `mode` = "append" the content is appended
	// to an existing file (creating it if absent). Returns the number of UTF-8
	// bytes written.
	class WriteFileTool : public engine::ExecutableTool
	{
	public:
		std::string Name() const override
		{
			return "Write";
		}
		std::string Description() const override;
		nlohmann::json Parameters() const override;

		absl::StatusOr<engine::ToolExecution> ResolveExecution(const nlohmann::json& args) override;
		absl::StatusOr<engine::ToolResult> Execute(const nlohmann::json& args, const engine::ToolContext& ctx) override;
	};

} // namespace codeharness::tools
