#include "Tui/Components/ThinkingView.h"
#include "Tui/TuiState.h"

#include <doctest/doctest.h>

#include <string>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

namespace
{

std::string RenderToString(const ftxui::Element& element, int width = 100, int height = 10)
{
	ftxui::Screen screen(width, height);
	ftxui::Render(screen, element);
	return screen.ToString();
}

} // namespace

TEST_CASE("ThinkingView: collapsed preview renders bullet and hint")
{
	auto out = RenderToString(codeharness::tui::ThinkingView::Render("one\ntwo\nthree\nfour\nfive", false));
	CHECK(out.find("\xE2\x97\x8F") != std::string::npos); // U+25CF status bullet.
	CHECK(out.find("one") != std::string::npos);
	CHECK(out.find("three") != std::string::npos);
	CHECK(out.find("five") == std::string::npos);
	CHECK(out.find("2 more lines") != std::string::npos);
	CHECK(out.find("ctrl+o to expand") != std::string::npos);
}

TEST_CASE("ThinkingView: expanded renders full text")
{
	auto out = RenderToString(codeharness::tui::ThinkingView::Render("one\ntwo\nthree\nfour\nfive", true));
	CHECK(out.find("one") != std::string::npos);
	CHECK(out.find("five") != std::string::npos);
	CHECK(out.find("ctrl+o to expand") == std::string::npos);
}

TEST_CASE("TuiState: applies tool output expansion globally")
{
	codeharness::tui::TuiState state;
	codeharness::tui::ToolCallState active;
	active.id = "active";
	active.name = "Bash";
	codeharness::tui::ToolCallState completed;
	completed.id = "done";
	completed.name = "Read";
	state.activeToolCalls.emplace(active.id, active);
	state.completedToolCalls.emplace(completed.id, completed);

	codeharness::tui::ApplyToolOutputExpanded(state, true);
	CHECK(state.toolOutputExpanded);
	CHECK(state.activeToolCalls.at("active").expanded);
	CHECK(state.completedToolCalls.at("done").expanded);

	codeharness::tui::ApplyToolOutputExpanded(state, false);
	CHECK_FALSE(state.toolOutputExpanded);
	CHECK_FALSE(state.activeToolCalls.at("active").expanded);
	CHECK_FALSE(state.completedToolCalls.at("done").expanded);
}

