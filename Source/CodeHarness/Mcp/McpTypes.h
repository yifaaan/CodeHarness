#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace codeharness::mcp
{

	struct McpServerConfig
	{
		std::string name;
		std::string command;
		std::vector<std::string> args;
		std::map<std::string, std::string> env;
		std::string cwd;
		bool enabled = true;
	};

	struct McpToolDefinition
	{
		std::string name;
		std::string description;
		nlohmann::json inputSchema = nlohmann::json::object();
	};

	struct McpToolResult
	{
		std::string content;
		bool isError = false;
	};

} // namespace codeharness::mcp
