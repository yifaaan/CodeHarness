#include "Tui/Components/HelpDialog.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

namespace codeharness::tui
{
	namespace
	{
		bool SkillCommand(const SlashCommands::Command& command)
		{
			return command.name.rfind("skill:", 0) == 0;
		}

		std::string CommandLabel(const SlashCommands::Command& command)
		{
			std::string out = "/" + command.name;
			if (!command.aliases.empty())
			{
				out += " (";
				for (std::size_t i = 0; i < command.aliases.size(); ++i)
				{
					if (i > 0)
					{
						out += ", ";
					}
					out += "/" + command.aliases[i];
				}
				out += ")";
			}
			return out;
		}

		std::vector<SlashCommands::Command> SortedCommands(std::vector<SlashCommands::Command> commands)
		{
			std::stable_sort(commands.begin(), commands.end(), [](const auto& lhs, const auto& rhs) {
				if (SkillCommand(lhs) != SkillCommand(rhs))
				{
					return !SkillCommand(lhs);
				}
				return lhs.name < rhs.name;
			});
			return commands;
		}

		class HelpDialogComponent : public ftxui::ComponentBase
		{
		public:
			explicit HelpDialogComponent(HelpDialogOptions options)
				: options(std::move(options))
			{
				this->options.commands = SortedCommands(std::move(this->options.commands));
				if (this->options.maxVisible <= 0)
				{
					this->options.maxVisible = 24;
				}
			}

			bool Focusable() const override
			{
				return true;
			}

			bool OnEvent(ftxui::Event event) override
			{
				if (event == ftxui::Event::Escape || event == ftxui::Event::Return ||
					event == ftxui::Event::q || event == ftxui::Event::Q)
				{
					if (options.onClose)
					{
						options.onClose();
					}
					return true;
				}
				if (event == ftxui::Event::ArrowUp)
				{
					scrollTop = std::max(0, scrollTop - 1);
					return true;
				}
				if (event == ftxui::Event::ArrowDown)
				{
					++scrollTop;
					return true;
				}
				if (event == ftxui::Event::PageUp)
				{
					scrollTop = std::max(0, scrollTop - 10);
					return true;
				}
				if (event == ftxui::Event::PageDown)
				{
					scrollTop += 10;
					return true;
				}
				return false;
			}

			ftxui::Element OnRender() override
			{
				using namespace ftxui;
				Elements content;
				content.push_back(text(" help ") | bold | color(Color::Cyan));
				content.push_back(text(" Esc / Enter / q close, Up/Down scroll") | dim);
				content.push_back(text(""));
				content.push_back(text("Keyboard shortcuts") | bold);
				content.push_back(text("  Shift-Tab              Toggle plan mode") | dim);
				content.push_back(text("  Ctrl-G                 Edit in external editor") | dim);
				content.push_back(text("  Ctrl-O                 Toggle tool output expansion") | dim);
				content.push_back(text("  Shift-Enter / Ctrl-J   Insert newline") | dim);
				content.push_back(text("  Ctrl-C                 Interrupt stream / clear input") | dim);
				content.push_back(text("  Ctrl-D                 Exit on empty input") | dim);
				content.push_back(text("  Esc                    Close dialogs / interrupt streaming") | dim);
				content.push_back(text("  Enter                  Submit") | dim);
				content.push_back(text(""));
				content.push_back(text("Slash commands") | bold);
				for (const auto& command : options.commands)
				{
					content.push_back(hbox({
						text("  " + CommandLabel(command)) | color(Color::Cyan),
						text("  " + command.description) | dim,
					}));
				}

				const int maxVisible = std::max(5, options.maxVisible);
				const int maxScroll = std::max(0, static_cast<int>(content.size()) - maxVisible);
				scrollTop = std::min(scrollTop, maxScroll);

				Elements shown;
				const int end = std::min(static_cast<int>(content.size()), scrollTop + maxVisible);
				for (int i = scrollTop; i < end; ++i)
				{
					shown.push_back(content[static_cast<std::size_t>(i)]);
				}
				if (static_cast<int>(content.size()) > maxVisible)
				{
					shown.push_back(text(fmt::format(" showing {}-{} of {}", scrollTop + 1, end, content.size())) | dim);
				}
				return vbox(std::move(shown)) | border | size(WIDTH, LESS_THAN, 100) | center;
			}

		private:
			HelpDialogOptions options;
			int scrollTop = 0;
		};
	} // namespace

	ftxui::Component HelpDialog::Create(HelpDialogOptions options)
	{
		return std::make_shared<HelpDialogComponent>(std::move(options));
	}

} // namespace codeharness::tui
