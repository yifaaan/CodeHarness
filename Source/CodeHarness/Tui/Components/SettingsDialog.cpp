#include "Tui/Components/SettingsDialog.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Tui/Components/ChoicePicker.h"

namespace codeharness::tui
{
	namespace
	{
		std::string PermissionLabel(config::PermissionMode mode)
		{
			switch (mode)
			{
			case config::PermissionMode::Yolo:
				return "YOLO";
			case config::PermissionMode::Auto:
				return "Auto";
			case config::PermissionMode::Manual:
			default:
				return "Manual";
			}
		}

		std::vector<ChoicePickerRow> SettingsRows(const SettingsDialogOptions& options)
		{
			auto currentModel = options.currentModel ? options.currentModel() : std::string{};
			auto permission = PermissionLabel(options.currentPermissionMode ? options.currentPermissionMode() : config::PermissionMode::Manual);
			return {
				ChoicePickerRow{.id = "model", .label = "Model", .description = "Current: " + currentModel},
				ChoicePickerRow{.id = "permission", .label = "Permission", .description = "Current: " + permission},
				ChoicePickerRow{.id = "theme", .label = "Theme", .description = "Terminal UI color theme"},
				ChoicePickerRow{.id = "editor", .label = "Editor", .description = "External editor command"},
				ChoicePickerRow{.id = "usage", .label = "Usage", .description = "Session tokens and context window"},
			};
		}

		SettingsSelection SelectionFromId(const std::string& id)
		{
			if (id == "model")
			{
				return SettingsSelection::Model;
			}
			if (id == "permission")
			{
				return SettingsSelection::Permission;
			}
			if (id == "theme")
			{
				return SettingsSelection::Theme;
			}
			if (id == "editor")
			{
				return SettingsSelection::Editor;
			}
			if (id == "usage")
			{
				return SettingsSelection::Usage;
			}
			return SettingsSelection::Model;
		}
	} // namespace

	ftxui::Component SettingsDialog::Create(SettingsDialogOptions options)
	{
		auto shared = std::make_shared<SettingsDialogOptions>(std::move(options));
		return ChoicePicker::Create(ChoicePickerOptions{
			.title = "Settings",
			.subtitle = "Choose a setting to configure",
			.rowSource = [shared] { return SettingsRows(*shared); },
			.onSelect = [shared](ChoicePickerRow row) {
				if (shared->onSelect)
				{
					shared->onSelect(SelectionFromId(row.id));
				}
			},
			.onCancel = [shared] {
				if (shared->onCancel)
				{
					shared->onCancel();
				}
			},
		});
	}

} // namespace codeharness::tui
