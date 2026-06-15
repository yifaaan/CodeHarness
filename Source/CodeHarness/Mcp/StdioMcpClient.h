#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include "Host/Host.h"
#include "Host/HostProcess.h"
#include "Mcp/McpClient.h"
#include "Mcp/McpTypes.h"

namespace codeharness::mcp
{

	class StdioMcpClient : public McpClient
	{
	public:
		StdioMcpClient(host::Host* host, McpServerConfig config);
		~StdioMcpClient() override;

		StdioMcpClient(const StdioMcpClient&) = delete;
		StdioMcpClient& operator=(const StdioMcpClient&) = delete;

		absl::Status Initialize(std::stop_token stopToken = {}) override;
		absl::StatusOr<std::vector<McpToolDefinition>> ListTools(std::stop_token stopToken = {}) override;
		absl::StatusOr<McpToolResult> CallTool(
			std::string_view name,
			const nlohmann::json& arguments,
			std::stop_token stopToken = {}) override;
		absl::Status Shutdown() override;

		const std::string& ServerName() const { return config.name; }

	private:
		absl::Status EnsureStarted();
		absl::Status Send(const nlohmann::json& message);
		absl::StatusOr<nlohmann::json> Request(std::string_view method, nlohmann::json params, std::stop_token stopToken);
		absl::StatusOr<nlohmann::json> WaitForResponse(int id, std::stop_token stopToken);

		host::Host* host = nullptr;
		McpServerConfig config;
		std::unique_ptr<host::HostProcess> process;
		std::string stdoutBuffer;
		std::string stderrBuffer;
		int nextId = 1;
		bool initialized = false;
		bool shutdown = false;
	};

} // namespace codeharness::mcp
