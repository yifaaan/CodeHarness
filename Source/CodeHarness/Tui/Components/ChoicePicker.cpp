#include "Tui/Components/ChoicePicker.h"

#include <algorithm>
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
		class ChoicePickerComponent : public ftxui::ComponentBase
		{
		public:
			explicit ChoicePickerComponent(ChoicePickerOptions options)
				: options(std::move(options))
			{
			}

			bool Focusable() const override
			{
				return true;
			}

			bool OnEvent(ftxui::Event event) override
			{
				if (event == ftxui::Event::Escape || event == ftxui::Event::Character('q') || event == ftxui::Event::Character('Q'))
				{
					if (options.onCancel)
					{
						options.onCancel();
					}
					return true;
				}
				if (event == ftxui::Event::ArrowUp)
				{
					Move(-1, Rows());
					return true;
				}
				if (event == ftxui::Event::ArrowDown)
				{
					Move(1, Rows());
					return true;
				}
				if (event == ftxui::Event::Return || event == ftxui::Event::Character(' '))
				{
					Activate();
					return true;
				}
				return false;
			}

			ftxui::Element OnRender() override
			{
				using namespace ftxui;
				auto rows = Rows();
				ClampSelection(rows);
				Elements lines;
				lines.push_back(text(" " + options.title + " ") | bold | color(Color::Cyan));
				if (!options.subtitle.empty())
				{
					lines.push_back(text(" " + options.subtitle) | dim);
				}
				lines.push_back(text(""));
				if (rows.empty())
				{
					lines.push_back(text("  No options available.") | dim);
				}
				for (int i = 0; i < static_cast<int>(rows.size()); ++i)
				{
					const auto& row = rows[static_cast<std::size_t>(i)];
					auto marker = row.current ? " * " : "   ";
					auto prefix = i == selected ? "> " : "  ";
					auto suffix = row.disabled ? "  not implemented" : "";
					auto label = text(prefix + std::string(marker) + row.label + suffix);
					if (row.disabled)
					{
						label = label | dim;
					}
					else if (i == selected)
					{
						label = label | bold | color(Color::Cyan);
					}
					lines.push_back(label);
					if (!row.description.empty())
					{
						lines.push_back(text("      " + row.description) | dim);
					}
				}
				lines.push_back(text(""));
				lines.push_back(text(" Up/Down select, Enter confirm, Esc back") | dim);
				return vbox(std::move(lines)) | border | size(WIDTH, LESS_THAN, 90) | center;
			}

		private:
			std::vector<ChoicePickerRow> Rows() const
			{
				return options.rowSource ? options.rowSource() : options.rows;
			}

			void ClampSelection(const std::vector<ChoicePickerRow>& rows)
			{
				if (selected < 0)
				{
					selected = 0;
				}
				if (selected >= static_cast<int>(rows.size()))
				{
					selected = std::max(0, static_cast<int>(rows.size()) - 1);
				}
			}

			void Move(int delta, const std::vector<ChoicePickerRow>& rows)
			{
				if (rows.empty())
				{
					selected = 0;
					return;
				}
				selected = (selected + delta + static_cast<int>(rows.size())) % static_cast<int>(rows.size());
			}

			void Activate()
			{
				auto rows = Rows();
				if (!options.onSelect || rows.empty())
				{
					return;
				}
				ClampSelection(rows);
				const auto& row = rows[static_cast<std::size_t>(selected)];
				if (row.disabled)
				{
					return;
				}
				options.onSelect(row);
			}

			ChoicePickerOptions options;
			int selected = 0;
		};
	} // namespace

	ftxui::Component ChoicePicker::Create(ChoicePickerOptions options)
	{
		return std::make_shared<ChoicePickerComponent>(std::move(options));
	}

} // namespace codeharness::tui
