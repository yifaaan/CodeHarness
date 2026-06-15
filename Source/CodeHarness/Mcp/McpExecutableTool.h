#pragma once

#include <absl/status/statusor.h>

#include <string>
#include <string_view>

#include "Engine/Tool.h"
#include "Mcp/McpClient.h"
#include "Mcp/McpTypes.h"

namespace codeharness::mcp
{

	class McpExecutableTool : public engine::ExecutableTool
	{
	public:
		McpExecutableTool(std::string qualifiedName, std::string serverName, McpToolDefinition definition, McpClient* client);

		std::string Name() const override;
		std::string Description() const override;
		nlohmann::json Parameters() const override;

		absl::StatusOr<engine::ToolExecution> ResolveExecution(const nlohmann::json& args) override;
		absl::StatusOr<engine::ToolResult> Execute(const nlohmann::json& args, const engine::ToolContext& ctx) override;

	private:
		std::string qualifiedName;
		std::string serverName;
		McpToolDefinition definition;
		McpClient* client = nullptr;
	};

	std::string QualifyMcpToolName(std::string_view serverName, std::string_view toolName);

} // namespace codeharness::mcp
