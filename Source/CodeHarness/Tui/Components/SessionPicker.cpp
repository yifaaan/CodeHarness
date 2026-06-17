#include "Tui/Components/SessionPicker.h"

#include <algorithm>
#include <chrono>
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
		std::string DisplayTitle(const session::SessionInfo& info)
		{
			return info.title.empty() ? "(untitled)" : info.title;
		}

		std::string RelativeTime(std::int64_t createdAt, std::int64_t updatedAt)
		{
			auto ts = updatedAt > 0 ? updatedAt : createdAt;
			if (ts <= 0)
			{
				return "";
			}
			const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
								 std::chrono::system_clock::now().time_since_epoch())
								 .count();
			const auto diffSeconds = std::max<std::int64_t>(0, (now - ts) / 1000);
			if (diffSeconds < 60)
			{
				return "just now";
			}
			const auto minutes = diffSeconds / 60;
			if (minutes < 60)
			{
				return fmt::format("{}m ago", minutes);
			}
			const auto hours = minutes / 60;
			if (hours < 24)
			{
				return fmt::format("{}h ago", hours);
			}
			return fmt::format("{}d ago", hours / 24);
		}

		class SessionPickerComponentImpl : public ftxui::ComponentBase
		{
		public:
			explicit SessionPickerComponentImpl(SessionPickerOptions options)
				: options(std::move(options))
			{
			}

			bool Focusable() const override
			{
				return true;
			}

			bool OnEvent(ftxui::Event event) override
			{
				if (event == ftxui::Event::CtrlC)
				{
					if (options.onCtrlC)
					{
						options.onCtrlC();
					}
					return true;
				}
				if (event == ftxui::Event::CtrlD)
				{
					if (options.onCtrlD)
					{
						options.onCtrlD();
					}
					return true;
				}
				if (event == ftxui::Event::CtrlA)
				{
					if (options.onToggleScope)
					{
						options.onToggleScope();
					}
					return true;
				}
				if (event == ftxui::Event::Escape)
				{
					if (!query.empty())
					{
						query.clear();
						selected = 0;
						return true;
					}
					if (options.onCancel)
					{
						options.onCancel();
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
					selected = std::min(selected + 1, std::max(0, static_cast<int>(VisibleSessions().size()) - 1));
					return true;
				}
				if (event == ftxui::Event::Return)
				{
					auto sessions = VisibleSessions();
					if (!sessions.empty() && selected >= 0 && selected < static_cast<int>(sessions.size()) && options.onSelect)
					{
						options.onSelect(sessions[static_cast<std::size_t>(selected)]);
					}
					return true;
				}
				if (event.is_character())
				{
					auto ch = event.character();
					if (!ch.empty() && ch[0] >= 32)
					{
						query.push_back(ch[0]);
						selected = 0;
						return true;
					}
				}
				return false;
			}

			ftxui::Element OnRender() override
			{
				using namespace ftxui;
				auto sessions = VisibleSessions();
				Elements lines;
				lines.push_back(text(" Sessions ") | bold | color(Color::Cyan));
				lines.push_back(text(" Esc clears search, Enter selects, Ctrl+A toggles scope") | dim);
				lines.push_back(text(""));
				if (!query.empty())
				{
					lines.push_back(text("Search: " + query) | color(Color::Yellow));
				}
				if (sessions.empty())
				{
					lines.push_back(text("No sessions found.") | dim);
				}
				for (int i = 0; i < static_cast<int>(sessions.size()); ++i)
				{
					const auto& info = sessions[static_cast<std::size_t>(i)];
					auto prefix = i == selected ? "> " : "  ";
					const auto current = options.currentSessionId && options.currentSessionId() == info.sessionId ? " current" : "";
					auto header = text(fmt::format("{}{}  {}{}", prefix, DisplayTitle(info), RelativeTime(info.createdAt, info.updatedAt), current));
					if (i == selected)
					{
						header = header | bold | color(Color::Cyan);
					}
					else
					{
						header = header | dim;
					}
					lines.push_back(header);
					lines.push_back(text("    " + info.sessionId) | color(Color::GrayLight));
					lines.push_back(text("    " + info.workdir) | dim);
				}
				return vbox(std::move(lines)) | border | size(WIDTH, LESS_THAN, 100) | center;
			}

		private:
			std::vector<session::SessionInfo> VisibleSessions() const
			{
				auto out = options.sessions ? options.sessions() : std::vector<session::SessionInfo>{};
				if (query.empty())
				{
					return out;
				}
				std::vector<session::SessionInfo> filtered;
				for (const auto& session : out)
				{
					if (DisplayTitle(session).find(query) != std::string::npos || session.sessionId.find(query) != std::string::npos)
					{
						filtered.push_back(session);
					}
				}
				return filtered;
			}

			SessionPickerOptions options;
			std::string query;
			int selected = 0;
		};
	} // namespace

	ftxui::Component SessionPicker::Create(SessionPickerOptions options)
	{
		return std::make_shared<SessionPickerComponentImpl>(std::move(options));
	}

} // namespace codeharness::tui
