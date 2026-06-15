#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <stop_token>
#include <string_view>
#include <vector>

#include "Mcp/McpTypes.h"

namespace codeharness::mcp
{

	class McpClient
	{
	public:
		virtual ~McpClient() = default;

		virtual absl::Status Initialize(std::stop_token stopToken = {}) = 0;
		virtual absl::StatusOr<std::vector<McpToolDefinition>> ListTools(std::stop_token stopToken = {}) = 0;
		virtual absl::StatusOr<McpToolResult> CallTool(
			std::string_view name,
			const nlohmann::json& arguments,
			std::stop_token stopToken = {}) = 0;
		virtual absl::Status Shutdown() = 0;
	};

} // namespace codeharness::mcp
