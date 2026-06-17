#include "Tui/Components/Banner.h"

#include <mutex>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "Tui/TuiState.h"

namespace codeharness::tui
{

	ftxui::Component Banner::Create(std::shared_ptr<TuiState> state)
	{
		using namespace ftxui;
		return Renderer([state = std::move(state)] {
			std::lock_guard<std::mutex> lk(state->mutex);
			if (state->sessionId.empty() || state->transcript.empty())
			{
				return text("");
			}

			return vbox({
				hbox({
					text("✓ CodeHarness ") | bold | color(Color::Cyan),
					text("Ready for this workspace") | bold,
				}),
				hbox({
					text("  "),
					text("Send /help for commands, /model to switch model, Ctrl+B for details") |
						dim | color(Color::GrayLight),
				}),
				text(""),
			});
		});
	}

} // namespace codeharness::tui
