#include "Tui/Components/QueuePanel.h"

#include <mutex>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "Tui/TuiState.h"

namespace codeharness::tui
{

	ftxui::Component QueuePanel::Create(std::shared_ptr<TuiState> state)
	{
		using namespace ftxui;
		return Renderer([state = std::move(state)] {
			std::lock_guard<std::mutex> lk(state->mutex);
			if (state->pendingToolCalls.empty())
			{
				return text("");
			}

			Elements rows;
			rows.push_back(text(" Queue") | bold | color(Color::Magenta));
			for (const auto& q : state->pendingToolCalls)
			{
				std::string label = q.preview.empty() ? q.name : (q.name + " " + q.preview);
				if (label.size() > 60)
					label = label.substr(0, 57) + "...";
				rows.push_back(hbox({
					text("  ⏸ ") | color(Color::Magenta),
					text(label) | color(Color::GrayLight),
				}));
			}
			return vbox(std::move(rows)) | ftxui::borderLight | color(Color::Magenta);
		});
	}

} // namespace codeharness::tui
