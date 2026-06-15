#pragma once

#include <absl/status/status.h>

#include <memory>
#include <vector>

#include "Host/Host.h"
#include "Mcp/McpClient.h"
#include "Mcp/McpTypes.h"
#include "Tools/ToolManager.h"

namespace codeharness::mcp
{

	class McpConnectionManager
	{
	public:
		McpConnectionManager(host::Host* host, std::vector<McpServerConfig> servers);
		~McpConnectionManager();

		McpConnectionManager(const McpConnectionManager&) = delete;
		McpConnectionManager& operator=(const McpConnectionManager&) = delete;

		// Best-effort: logs and skips failed servers/tools, but returns OK so the
		// rest of the harness can continue without external MCP tools.
		absl::Status RegisterTools(tools::ToolManager& toolManager);
		absl::Status Shutdown();

	private:
		host::Host* host = nullptr;
		std::vector<McpServerConfig> servers;
		std::vector<std::unique_ptr<McpClient>> clients;
	};

} // namespace codeharness::mcp
