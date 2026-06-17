#include "Tui/Components/QuestionDialog.h"

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
		class QuestionDialogComponentImpl : public ftxui::ComponentBase
		{
		public:
			explicit QuestionDialogComponentImpl(QuestionDialogOptions options)
				: options(std::move(options))
			{
			}

			bool Focusable() const override
			{
				return true;
			}

			bool OnEvent(ftxui::Event event) override
			{
				if (!request())
				{
					return false;
				}
				if (event == ftxui::Event::Escape || event == ftxui::Event::CtrlC || event == ftxui::Event::CtrlD)
				{
					if (options.onAnswer)
					{
						options.onAnswer("");
					}
					return true;
				}
				if (event == ftxui::Event::CtrlO)
				{
					if (options.onToggleToolOutput)
					{
						options.onToggleToolOutput();
					}
					return true;
				}
				if (event == ftxui::Event::ArrowUp)
				{
					selected = std::max(0, selected - 1);
					return true;
				}
				if (event == ftxui::Event::ArrowDown)
				{
					auto req = request();
					const auto maxIndex = req ? MaxSelectableIndex(*req) : 0;
					selected = std::min(selected + 1, maxIndex);
					return true;
				}
				if (event == ftxui::Event::Return)
				{
					Submit();
					return true;
				}
				if (event.is_character())
				{
					auto ch = event.character();
					if (!ch.empty() && ch[0] >= 32)
					{
						auto req = request();
						if (req && selected == static_cast<int>(req->options.size()) && req->allowFreeform)
						{
							freeform.push_back(ch[0]);
							return true;
						}
						auto numeric = ch[0] - '1';
						if (req && numeric >= 0 && numeric <= MaxSelectableIndex(*req))
						{
							selected = numeric;
							if (selected < static_cast<int>(req->options.size()))
							{
								Submit();
							}
							return true;
						}
					}
				}
				if ((event == ftxui::Event::Backspace || event == ftxui::Event::CtrlH) && !freeform.empty())
				{
					freeform.pop_back();
					return true;
				}
				return false;
			}

			ftxui::Element OnRender() override
			{
				using namespace ftxui;
				auto req = request();
				if (!req)
				{
					return text("");
				}

				Elements lines;
				lines.push_back(text(" Question ") | bold | color(Color::Cyan));
				lines.push_back(text(" Enter confirm, Esc cancel, Ctrl+O toggle tool output") | dim);
				lines.push_back(text(""));
				lines.push_back(text("  " + req->question) | bold);
				lines.push_back(text(""));

				for (std::size_t i = 0; i < req->options.size(); ++i)
				{
					auto prefix = static_cast<int>(i) == selected ? "> " : "  ";
					auto line = text(fmt::format("  {}{}. {}", prefix, i + 1, req->options[i]));
					if (static_cast<int>(i) == selected)
					{
						line = line | bold | color(Color::Yellow);
					}
					else
					{
						line = line | color(Color::GrayLight);
					}
					lines.push_back(line);
				}

				if (req->allowFreeform)
				{
					auto freeIndex = static_cast<int>(req->options.size());
					auto prefix = selected == freeIndex ? "> " : "  ";
					auto line = text(fmt::format("  {}{}. (custom answer)", prefix, freeIndex + 1));
					if (selected == freeIndex)
					{
						line = line | bold | color(Color::Yellow);
					}
					else
					{
						line = line | color(Color::GrayLight);
					}
					lines.push_back(line);
					if (selected == freeIndex)
					{
						lines.push_back(text("    " + freeform + "|") | color(Color::Yellow));
					}
				}

				return vbox(std::move(lines)) | border | size(WIDTH, LESS_THAN, 90) | center;
			}

		private:
			std::optional<tools::QuestionRequest> request() const
			{
				return options.request ? options.request() : std::nullopt;
			}

			static int MaxSelectableIndex(const tools::QuestionRequest& req)
			{
				return static_cast<int>(req.options.size()) + (req.allowFreeform ? 1 : 0) - 1;
			}

			void Submit()
			{
				auto req = request();
				if (!req || !options.onAnswer)
				{
					return;
				}
				std::string answer;
				if (selected >= 0 && selected < static_cast<int>(req->options.size()))
				{
					answer = req->options[static_cast<std::size_t>(selected)];
				}
				else if (allowFreeform())
				{
					answer = freeform;
				}
				options.onAnswer(answer);
			}

			bool allowFreeform() const
			{
				auto req = request();
				return req && req->allowFreeform;
			}

			QuestionDialogOptions options;
			int selected = 0;
			std::string freeform;
		};
	} // namespace

	ftxui::Component QuestionDialog::Create(QuestionDialogOptions options)
	{
		return std::make_shared<QuestionDialogComponentImpl>(std::move(options));
	}

} // namespace codeharness::tui
