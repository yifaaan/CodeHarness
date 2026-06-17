#pragma once

#include <functional>
#include <optional>
#include <string>

#include <ftxui/component/component.hpp>
#include <nlohmann/json.hpp>

#include "Permission/PermissionTypes.h"

namespace codeharness::tui
{

	struct ApprovalPanelRequest
	{
		std::string toolName;
		nlohmann::json args;
		std::string description;
	};

	struct ApprovalPanelResponse
	{
		permission::PermissionDecision decision = permission::PermissionDecision::Deny;
		std::string selectedLabel;
		std::string feedback;
	};

	struct ApprovalPanelOptions
	{
		std::function<std::optional<ApprovalPanelRequest>()> request;
		std::function<void(ApprovalPanelResponse)> onResponse;
		std::function<void()> onToggleToolOutput;
	};

	class ApprovalPanel
	{
	public:
		static ftxui::Component Create(ApprovalPanelOptions options);
	};

} // namespace codeharness::tui
