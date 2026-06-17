#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace codeharness::desktop
{

	enum class DesktopMessageRole
	{
		User,
		Assistant,
		System,
		Tool,
	};

	struct DesktopMessage
	{
		std::string id;
		DesktopMessageRole role = DesktopMessageRole::Assistant;
		std::string text;
		bool isStreaming = false;
		bool isError = false;
	};

	struct DesktopToolCall
	{
		std::string id;
		std::string name;
		nlohmann::json args;
		std::string result;
		bool isError = false;
		bool completed = false;
	};

	struct DesktopSessionItem
	{
		std::string sessionId;
		std::string title;
		std::string workdir;
		std::int64_t createdAt = 0;
		std::int64_t updatedAt = 0;
	};

	struct DesktopPermissionRequest
	{
		std::string toolName;
		nlohmann::json args;
		std::string description;
	};

	struct DesktopEvent
	{
		std::string sessionId;
		std::string agentId;
		std::string type;
		nlohmann::json payload;
	};

} // namespace codeharness::desktop
