#include "Tui/Components/ApprovalPanel.h"

#include <algorithm>
#include <array>
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
		struct Choice
		{
			std::string label;
			permission::PermissionDecision decision = permission::PermissionDecision::Deny;
			bool requiresFeedback = false;
		};

		const std::array<Choice, 4>& Choices()
		{
			static const std::array<Choice, 4> choices = {
				Choice{.label = "Approve once", .decision = permission::PermissionDecision::Allow},
				Choice{.label = "Approve for this session", .decision = permission::PermissionDecision::Allow},
				Choice{.label = "Reject", .decision = permission::PermissionDecision::Deny},
				Choice{.label = "Reject with feedback", .decision = permission::PermissionDecision::Deny, .requiresFeedback = true},
			};
			return choices;
		}

		std::vector<std::string> SplitLines(std::string_view text)
		{
			std::vector<std::string> out;
			std::size_t start = 0;
			while (start <= text.size())
			{
				auto end = text.find('\n', start);
				if (end == std::string_view::npos)
				{
					out.push_back(std::string(text.substr(start)));
					break;
				}
				out.push_back(std::string(text.substr(start, end - start)));
				start = end + 1;
			}
			return out;
		}

		std::string FirstLine(std::string_view text)
		{
			auto end = text.find('\n');
			return std::string(text.substr(0, end == std::string_view::npos ? text.size() : end));
		}

		std::string HeaderFor(std::string_view toolName)
		{
			if (toolName == "Bash")
			{
				return "Run this command?";
			}
			if (toolName == "Write")
			{
				return "Write this file?";
			}
			if (toolName == "Edit")
			{
				return "Apply these edits?";
			}
			if (toolName == "ExitPlanMode")
			{
				return "Ready to build with this plan?";
			}
			return fmt::format("Approve {}?", toolName);
		}

		bool IsNumericChoice(const ftxui::Event& event, int& index)
		{
			for (int i = 0; i < static_cast<int>(Choices().size()); ++i)
			{
				if (event == ftxui::Event::Character(static_cast<char>('1' + i)))
				{
					index = i;
					return true;
				}
			}
			return false;
		}

		class ApprovalPanelComponent : public ftxui::ComponentBase
		{
		public:
			explicit ApprovalPanelComponent(ApprovalPanelOptions options)
				: options(std::move(options))
			{
			}

			bool Focusable() const override
			{
				return true;
			}

			bool OnEvent(ftxui::Event event) override
			{
				if (!options.request || !options.request().has_value())
				{
					return false;
				}

				if (event == ftxui::Event::Escape || event == ftxui::Event::CtrlC || event == ftxui::Event::CtrlD)
				{
					Submit(2, "");
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

				if (feedbackMode)
				{
					return HandleFeedbackEvent(event);
				}

				if (event == ftxui::Event::ArrowUp)
				{
					selectedIndex = (selectedIndex + static_cast<int>(Choices().size()) - 1) % static_cast<int>(Choices().size());
					return true;
				}
				if (event == ftxui::Event::ArrowDown)
				{
					selectedIndex = (selectedIndex + 1) % static_cast<int>(Choices().size());
					return true;
				}
				if (event == ftxui::Event::Return)
				{
					Activate(selectedIndex);
					return true;
				}

				int numericIndex = 0;
				if (IsNumericChoice(event, numericIndex))
				{
					selectedIndex = numericIndex;
					Activate(numericIndex);
					return true;
				}
				return false;
			}

			ftxui::Element OnRender() override
			{
				using namespace ftxui;
				auto request = options.request ? options.request() : std::nullopt;
				if (!request.has_value())
				{
					return text("");
				}

				Elements lines;
				lines.push_back(text(" " + HeaderFor(request->toolName) + " ") | bold | color(Color::Yellow));
				lines.push_back(separator());
				if (!request->description.empty())
				{
					lines.push_back(text("  " + FirstLine(request->description)) | color(Color::GrayLight));
				}
				lines.push_back(text("  Tool: " + request->toolName) | bold);

				auto args = request->args.dump(2);
				auto argLines = SplitLines(args);
				if (!argLines.empty())
				{
					lines.push_back(text(""));
					lines.push_back(text("  Args") | bold | color(Color::Cyan));
					const auto shown = std::min<std::size_t>(argLines.size(), 8);
					for (std::size_t i = 0; i < shown; ++i)
					{
						lines.push_back(text("    " + argLines[i]) | color(Color::GrayLight));
					}
					if (argLines.size() > shown)
					{
						lines.push_back(text(fmt::format("    ... {} more lines", argLines.size() - shown)) | dim);
					}
				}

				lines.push_back(text(""));
				for (int i = 0; i < static_cast<int>(Choices().size()); ++i)
				{
					const auto& choice = Choices()[i];
					auto prefix = i == selectedIndex ? "> " : "  ";
					auto label = fmt::format("{}{}. {}", prefix, i + 1, choice.label);
					if (feedbackMode && i == selectedIndex && choice.requiresFeedback)
					{
						label += "  " + feedback.substr(0, feedbackCursor) + "|" + feedback.substr(feedbackCursor);
					}
					auto line = text("  " + label);
					if (i == selectedIndex)
					{
						line = line | bold | color(Color::Yellow);
					}
					lines.push_back(line);
				}
				lines.push_back(text(""));
				lines.push_back(text(feedbackMode ? "  Type feedback, Enter submit, Esc reject" : "  Up/Down select, 1/2/3/4 choose, Enter confirm, Esc reject") | dim);
				lines.push_back(text("  Ctrl+O toggles tool output") | dim);
				return vbox(std::move(lines)) | border | size(WIDTH, LESS_THAN, 90) | center;
			}

		private:
			void Activate(int index)
			{
				const auto& choice = Choices()[index];
				if (choice.requiresFeedback)
				{
					feedbackMode = true;
					feedback.clear();
					feedbackCursor = 0;
					return;
				}
				Submit(index, "");
			}

			void Submit(int index, std::string feedbackText)
			{
				if (!options.onResponse)
				{
					return;
				}
				const auto& choice = Choices()[index];
				options.onResponse(ApprovalPanelResponse{
					.decision = choice.decision,
					.selectedLabel = choice.label,
					.feedback = std::move(feedbackText),
				});
				feedbackMode = false;
				feedback.clear();
				feedbackCursor = 0;
				selectedIndex = 0;
			}

			bool HandleFeedbackEvent(const ftxui::Event& event)
			{
				if (event == ftxui::Event::Return)
				{
					Submit(selectedIndex, feedback);
					return true;
				}
				if (event == ftxui::Event::Escape)
				{
					Submit(2, "");
					return true;
				}
				if (event == ftxui::Event::ArrowLeft)
				{
					if (feedbackCursor > 0)
					{
						--feedbackCursor;
					}
					return true;
				}
				if (event == ftxui::Event::ArrowRight)
				{
					if (feedbackCursor < feedback.size())
					{
						++feedbackCursor;
					}
					return true;
				}
				if (event == ftxui::Event::Backspace || event == ftxui::Event::CtrlH)
				{
					if (feedbackCursor > 0)
					{
						feedback.erase(feedbackCursor - 1, 1);
						--feedbackCursor;
					}
					return true;
				}
				if (event.is_character())
				{
					auto ch = event.character();
					if (!ch.empty() && ch[0] >= 32)
					{
						feedback.insert(feedbackCursor, ch);
						feedbackCursor += ch.size();
						return true;
					}
				}
				return false;
			}

			ApprovalPanelOptions options;
			int selectedIndex = 0;
			bool feedbackMode = false;
			std::string feedback;
			std::size_t feedbackCursor = 0;
		};
	} // namespace

	ftxui::Component ApprovalPanel::Create(ApprovalPanelOptions options)
	{
		return std::make_shared<ApprovalPanelComponent>(std::move(options));
	}

} // namespace codeharness::tui
