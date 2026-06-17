#include "Tui/Components/CompactionIndicator.h"

#include <memory>
#include <mutex>
#include <string>

#include <fmt/format.h>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "Tui/TuiState.h"

namespace codeharness::tui
{

	ftxui::Component CompactionIndicator::Create(std::shared_ptr<TuiState> state)
	{
		using namespace ftxui;
		return Renderer([state = std::move(state)] {
			std::lock_guard<std::mutex> lk(state->mutex);
			if (!state->compacting)
			{
				return text("");
			}
			return hbox({
					   text(" ⏳ ") | color(Color::Magenta) | bold,
					   text(fmt::format("Compacting context ({} messages)...", state->compactingCount)) | color(Color::Magenta),
				   }) |
				   borderRounded | color(Color::Magenta);
		});
	}

} // namespace codeharness::tui
