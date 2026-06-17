#include "Tui/Components/SettingsDialog.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

namespace codeharness::tui
{
	namespace
	{
		struct SettingItem
		{
			SettingsSelection value = SettingsSelection::Model;
			std::string label;
			std::string description;
		};

		const std::array<SettingItem, 7>& Items()
		{
			static const std::array<SettingItem, 7> items = {
				SettingItem{.value = SettingsSelection::Model, .label = "Model", .description = "Switch the active model and thinking mode."},
				SettingItem{.value = SettingsSelection::Permission, .label = "Permission", .description = "Choose how tool actions are approved."},
				SettingItem{.value = SettingsSelection::Theme, .label = "Theme", .description = "Change the terminal UI theme."},
				SettingItem{.value = SettingsSelection::Editor, .label = "Editor", .description = "Set the external editor command."},
				SettingItem{.value = SettingsSelection::Experiments, .label = "Experiments", .description = "Turn experimental features on or off."},
				SettingItem{.value = SettingsSelection::Upgrade, .label = "Automatic updates", .description = "Turn automatic CLI updates on or off."},
				SettingItem{.value = SettingsSelection::Usage, .label = "Usage", .description = "Show session tokens and context window."},
			};
			return items;
		}

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

		class SettingsDialogComponent : public ftxui::ComponentBase
		{
		public:
			explicit SettingsDialogComponent(SettingsDialogOptions options)
				: options(std::move(options))
			{
			}

			bool Focusable() const override
			{
				return true;
			}

			bool OnEvent(ftxui::Event event) override
			{
				if (event == ftxui::Event::Escape)
				{
					if (options.onCancel)
					{
						options.onCancel();
					}
					return true;
				}
				if (event == ftxui::Event::ArrowUp)
				{
					selected = (selected + static_cast<int>(Items().size()) - 1) % static_cast<int>(Items().size());
					return true;
				}
				if (event == ftxui::Event::ArrowDown)
				{
					selected = (selected + 1) % static_cast<int>(Items().size());
					return true;
				}
				if (event == ftxui::Event::Return || event == ftxui::Event::Character(' '))
				{
					if (options.onSelect)
					{
						options.onSelect(Items()[static_cast<std::size_t>(selected)].value);
					}
					return true;
				}
				return false;
			}

			ftxui::Element OnRender() override
			{
				using namespace ftxui;
				Elements lines;
				lines.push_back(text(" Settings ") | bold | color(Color::Cyan));
				lines.push_back(text(" Up/Down navigate, Enter select, Esc cancel") | dim);
				lines.push_back(text(""));
				lines.push_back(text(std::string("  Current model: ") + (options.currentModel ? options.currentModel() : std::string{})) | dim);
				lines.push_back(text(std::string("  Permission: ") + PermissionLabel(options.currentPermissionMode ? options.currentPermissionMode() : config::PermissionMode::Manual)) | dim);
				lines.push_back(text(""));
				for (int i = 0; i < static_cast<int>(Items().size()); ++i)
				{
					const auto& item = Items()[static_cast<std::size_t>(i)];
					std::string prefix = i == selected ? "> " : "  ";
					auto label = text(std::string("  ") + prefix + item.label);
					if (i == selected)
					{
						label = label | bold | color(Color::Cyan);
					}
					lines.push_back(label);
					lines.push_back(text(std::string("    ") + item.description) | dim);
				}
				return vbox(std::move(lines)) | border | size(WIDTH, LESS_THAN, 90) | center;
			}

		private:
			SettingsDialogOptions options;
			int selected = 0;
		};
	} // namespace

	ftxui::Component SettingsDialog::Create(SettingsDialogOptions options)
	{
		return std::make_shared<SettingsDialogComponent>(std::move(options));
	}

} // namespace codeharness::tui

