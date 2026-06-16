#include "Tui/Components/SidePanel.h"

#include <mutex>
#include <string>
#include <utility>

#include <fmt/format.h>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "Llm/Types.h"
#include "Tui/TuiState.h"

namespace codeharness::tui
{

namespace
{

int64_t TotalTokens(const llm::TokenUsage& u)
{
	return u.inputOther + u.output + u.inputCacheRead + u.inputCacheCreation;
}

std::string ModeLabel(config::PermissionMode m)
{
	switch (m)
	{
		case config::PermissionMode::Yolo: return "YOLO";
		case config::PermissionMode::Auto: return "Auto";
		case config::PermissionMode::Manual:
		default: return "Manual";
	}
}

} // namespace

ftxui::Component SidePanel::Create(std::shared_ptr<TuiState> state)
{
	using namespace ftxui;
	return Renderer([state = std::move(state)] {
		std::lock_guard<std::mutex> lk(state->mutex);
		if (!state->sidePanelVisible)
		{
			return text("");
		}

		std::string sessionIdShort = state->sessionId.empty()
										 ? "-"
										 : state->sessionId.substr(0, std::min<size_t>(8, state->sessionId.size()));
		std::string modelShort = state->model.empty() ? "no model" : state->model;
		auto tokens = TotalTokens(state->lastUsage);
		auto active = static_cast<int>(state->activeToolCalls.size());

		Elements rows;
		rows.push_back(text(" Session") | bold | color(Color::Cyan));
		rows.push_back(text(fmt::format("  {}", sessionIdShort)) | dim);
		rows.push_back(text(""));
		rows.push_back(text(" Model") | bold | color(Color::Cyan));
		rows.push_back(text(fmt::format("  {}", modelShort)) | dim);
		rows.push_back(text(""));
		rows.push_back(text(" Mode") | bold | color(Color::Cyan));
		rows.push_back(text(fmt::format("  {}", ModeLabel(state->permissionMode))) | dim);
		rows.push_back(text(""));
		rows.push_back(text(" Tokens") | bold | color(Color::Cyan));
		rows.push_back(text(fmt::format("  {}", tokens)) | dim);
		rows.push_back(text(""));
		rows.push_back(text(" Active tools") | bold | color(Color::Cyan));
		rows.push_back(text(fmt::format("  {}", active)) | dim);
		rows.push_back(text(""));
		rows.push_back(text(" Ctrl+B: hide") | dim | color(Color::GrayLight));

		return vbox(std::move(rows)) | border | size(WIDTH, EQUAL, 30);
	});
}

} // namespace codeharness::tui
