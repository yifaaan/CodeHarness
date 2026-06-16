#include "Tui/Components/WelcomePanel.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <utility>

#include <fmt/format.h>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "Tui/TuiState.h"

namespace codeharness::tui
{

namespace
{

std::string ShortSessionId(const std::string& id)
{
	if (id.empty())
	{
		return "-";
	}
	return id.substr(0, std::min<size_t>(12, id.size()));
}

} // namespace

ftxui::Component WelcomePanel::Create(std::shared_ptr<TuiState> state)
{
	using namespace ftxui;
	return Renderer([state = std::move(state)] {
		std::lock_guard<std::mutex> lk(state->mutex);
		if (!state->transcript.empty())
		{
			return text("");
		}

		const std::string model = state->model.empty() ? "not set, run /model" : state->model;
		const std::string workdir = state->workdir.empty() ? "-" : state->workdir;
		const std::string session = ShortSessionId(state->sessionId);

		Elements rows;
		rows.push_back(hbox({
			text("▛▀▜ ") | bold | color(Color::Cyan),
			text("Welcome to CodeHarness!") | bold | color(Color::Cyan),
		}));
		rows.push_back(hbox({
			text("▙▄▟ ") | bold | color(Color::Cyan),
			text("Send /help for help information.") | dim | color(Color::GrayLight),
		}));
		rows.push_back(separatorLight());
		rows.push_back(hbox({text("Directory: ") | bold | dim, text(workdir)}));
		rows.push_back(hbox({text("Session:   ") | bold | dim, text(session)}));
		rows.push_back(hbox({text("Model:     ") | bold | dim, text(model)}));
		rows.push_back(hbox({text("Version:   ") | bold | dim, text(state->version)}));
		if (!state->statusMessage.empty())
		{
			rows.push_back(hbox({text("Status:    ") | bold | dim, text(state->statusMessage) | dim}));
		}

		return vbox({
			text(""),
			vbox(std::move(rows)) | borderRounded | color(Color::Cyan),
			text(""),
		});
	});
}

} // namespace codeharness::tui
