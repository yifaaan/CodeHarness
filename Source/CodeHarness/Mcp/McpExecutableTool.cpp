#include "Mcp/McpExecutableTool.h"

#include <absl/status/status.h>
#include <fmt/format.h>

#include <cctype>
#include <cstdint>
#include <string>
#include <utility>

namespace codeharness::mcp
{
	namespace
	{

		std::string Sanitize(std::string_view value)
		{
			std::string out;
			out.reserve(value.size());
			for (char c : value)
			{
				const auto u = static_cast<unsigned char>(c);
				if (std::isalnum(u) || c == '_')
					out.push_back(static_cast<char>(std::tolower(u)));
				else
					out.push_back('_');
			}
			return out.empty() ? "unnamed" : out;
		}

		std::string ShortHash(std::string_view value)
		{
			std::uint64_t hash = 1469598103934665603ull;
			for (char c : value)
			{
				hash ^= static_cast<unsigned char>(c);
				hash *= 1099511628211ull;
			}
			return fmt::format("{:016x}", hash).substr(0, 8);
		}

	} // namespace

	std::string QualifyMcpToolName(std::string_view serverName, std::string_view toolName)
	{
		std::string full = fmt::format("mcp__{}__{}", Sanitize(serverName), Sanitize(toolName));
		if (full.size() <= 64)
			return full;
		const auto hash = ShortHash(full);
		auto server = Sanitize(serverName);
		auto tool = Sanitize(toolName);
		if (server.size() > 20)
			server.resize(20);
		const std::size_t prefix = std::string("mcp__").size() + server.size() + std::string("__").size() + hash.size() + 1;
		const std::size_t remaining = prefix >= 64 ? 0 : 64 - prefix;
		if (tool.size() > remaining)
			tool.resize(remaining);
		return fmt::format("mcp__{}__{}_{:s}", server, tool, hash);
	}

	McpExecutableTool::McpExecutableTool(std::string qName, std::string sName, McpToolDefinition def, McpClient* clientPtr)
		: qualifiedName(std::move(qName)), serverName(std::move(sName)), definition(std::move(def)), client(clientPtr)
	{
	}

	std::string McpExecutableTool::Name() const
	{
		return qualifiedName;
	}

	std::string McpExecutableTool::Description() const
	{
		if (!definition.description.empty())
			return fmt::format("[MCP:{}] {}", serverName, definition.description);
		return fmt::format("[MCP:{}] Call remote tool '{}'", serverName, definition.name);
	}

	nlohmann::json McpExecutableTool::Parameters() const
	{
		return definition.inputSchema.is_object() ? definition.inputSchema : nlohmann::json::object();
	}

	absl::StatusOr<engine::ToolExecution> McpExecutableTool::ResolveExecution(const nlohmann::json& args)
	{
		if (!args.is_object())
			return absl::InvalidArgumentError("MCP tool arguments must be an object");
		return engine::ToolExecution{
			.description = fmt::format("Call MCP tool {}.{}", serverName, definition.name),
			.requiresPermission = true,
		};
	}

	absl::StatusOr<engine::ToolResult> McpExecutableTool::Execute(const nlohmann::json& args, const engine::ToolContext& ctx)
	{
		if (client == nullptr)
			return absl::FailedPreconditionError("MCP client is not available");
		auto result = client->CallTool(definition.name, args, ctx.stopToken);
		if (!result.ok())
		{
			return engine::ToolResult{.content = result.status().ToString(), .isError = true};
		}
		return engine::ToolResult{.content = std::move(result->content), .isError = result->isError};
	}

} // namespace codeharness::mcp
