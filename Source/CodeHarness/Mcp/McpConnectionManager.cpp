#include "Mcp/McpConnectionManager.h"

#include <spdlog/spdlog.h>

#include <memory>
#include <utility>

#include "Mcp/McpExecutableTool.h"
#include "Mcp/StdioMcpClient.h"

namespace codeharness::mcp
{

	McpConnectionManager::McpConnectionManager(host::Host* hostPtr, std::vector<McpServerConfig> serverConfigs)
		: host(hostPtr), servers(std::move(serverConfigs))
	{
	}

	McpConnectionManager::~McpConnectionManager()
	{
		(void)Shutdown();
	}

	absl::Status McpConnectionManager::RegisterTools(tools::ToolManager& toolManager)
	{
		for (const auto& server : servers)
		{
			if (!server.enabled)
				continue;

			auto client = std::make_unique<StdioMcpClient>(host, server);
			auto toolDefs = client->ListTools();
			if (!toolDefs.ok())
			{
				spdlog::warn("mcp: server '{}' unavailable: {}", server.name, toolDefs.status().message());
				continue;
			}

			auto* clientPtr = client.get();
			std::size_t registered = 0;
			for (auto& def : *toolDefs)
			{
				auto qualified = QualifyMcpToolName(server.name, def.name);
				if (toolManager.Find(qualified) != nullptr)
				{
					spdlog::warn("mcp: skipping duplicate tool '{}'", qualified);
					continue;
				}
				toolManager.Register(std::make_unique<McpExecutableTool>(qualified, server.name, std::move(def), clientPtr));
				++registered;
			}
			spdlog::info("mcp: registered {} tools from server '{}'", registered, server.name);
			clients.push_back(std::move(client));
		}
		return absl::OkStatus();
	}

	absl::Status McpConnectionManager::Shutdown()
	{
		for (auto& client : clients)
		{
			if (client)
				(void)client->Shutdown();
		}
		clients.clear();
		return absl::OkStatus();
	}

} // namespace codeharness::mcp
