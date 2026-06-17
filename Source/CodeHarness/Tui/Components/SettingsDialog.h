#pragma once

#include <functional>
#include <string>

#include <ftxui/component/component.hpp>

#include "Config/ConfigTypes.h"

namespace codeharness::tui
{

	enum class SettingsSelection
	{
		Model,
		Permission,
		Theme,
		Editor,
		Experiments,
		Upgrade,
		Usage,
	};

	struct SettingsDialogOptions
	{
		std::function<std::string()> currentModel;
		std::function<config::PermissionMode()> currentPermissionMode;
		std::function<void(SettingsSelection)> onSelect;
		std::function<void()> onCancel;
	};

	class SettingsDialog
	{
	public:
		static ftxui::Component Create(SettingsDialogOptions options);
	};

} // namespace codeharness::tui
