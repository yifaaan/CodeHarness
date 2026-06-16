#include "Tui/Components/ActivityIndicator.h"

#include <mutex>
#include <utility>

#include <fmt/format.h>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "Tui/TuiState.h"

namespace codeharness::tui
{

ftxui::Component ActivityIndicator::Create(std::shared_ptr<TuiState> state)
{
	using namespace ftxui;
	return Renderer([state = std::move(state)] {
		std::lock_guard<std::mutex> lk(state->mutex);

		// No active work and no streaming → nothing to show.
		if (!state->streaming && state->activeToolCalls.empty() && state->currentActivity.empty())
		{
			return text("");
		}

		std::string label;
		if (!state->currentActivity.empty())
		{
			label = state->currentActivity;
		}
		else if (!state->activeToolCalls.empty())
		{
			// Use the most recent active tool call's name.
			const auto& tc = state->activeToolCalls.begin()->second;
			label = fmt::format("Running tool: {}", tc.name);
		}
		else
		{
			label = "Thinking";
		}

		return hbox({
				   text(" ⧗ ") | color(Color::Cyan),
				   text(label) | color(Color::Cyan) | dim,
			   });
	});
}

} // namespace codeharness::tui
