#include "Tui/Components/Banner.h"
#include "Tui/Components/StatusBar.h"
#include "Tui/Components/WelcomePanel.h"
#include "Tui/TuiState.h"

#include <doctest/doctest.h>

#include <memory>
#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

namespace
{

	std::string RenderComponent(ftxui::Component component, int width = 100, int height = 12)
	{
		ftxui::Screen screen(width, height);
		ftxui::Render(screen, component->Render());
		return screen.ToString();
	}

	std::shared_ptr<codeharness::tui::TuiState> MakeState()
	{
		auto state = std::make_shared<codeharness::tui::TuiState>();
		state->sessionId = "sess_1234567890abcdef";
		state->model = "gpt-5.5";
		state->workdir = "D:\\code\\CodeHarness";
		state->version = "CodeHarness v0.1.0";
		return state;
	}

} // namespace

TEST_CASE("ChromeComponents: welcome renders session model and unicode chrome")
{
	auto state = MakeState();
	auto output = RenderComponent(codeharness::tui::WelcomePanel::Create(state), 100, 14);

	CHECK(output.find("Welcome to CodeHarness") != std::string::npos);
	CHECK(output.find("gpt-5.5") != std::string::npos);
	CHECK(output.find("sess_1234567") != std::string::npos);
	CHECK(output.find("\xE2\x95\xAD") != std::string::npos); // U+256D box-drawing corner.
}

TEST_CASE("ChromeComponents: welcome hides once transcript has content")
{
	auto state = MakeState();
	state->transcript.push_back({.kind = codeharness::tui::TranscriptEntry::Kind::User, .text = "hi"});

	auto output = RenderComponent(codeharness::tui::WelcomePanel::Create(state), 80, 6);
	CHECK(output.find("Welcome to CodeHarness") == std::string::npos);
}

TEST_CASE("ChromeComponents: banner renders above active transcript")
{
	auto state = MakeState();
	state->transcript.push_back({.kind = codeharness::tui::TranscriptEntry::Kind::User, .text = "hi"});

	auto output = RenderComponent(codeharness::tui::Banner::Create(state), 100, 5);
	CHECK(output.find("CodeHarness") != std::string::npos);
	CHECK(output.find("/help") != std::string::npos);
	CHECK(output.find("\xE2\x9C\x93") != std::string::npos); // U+2713 check mark.
}

TEST_CASE("ChromeComponents: footer renders mode model activity and context")
{
	auto state = MakeState();
	state->permissionMode = codeharness::config::PermissionMode::Yolo;
	state->streaming = true;
	state->lastUsage.inputOther = 120;
	state->lastUsage.output = 30;

	auto output = RenderComponent(codeharness::tui::StatusBar::Create(state), 100, 5);
	CHECK(output.find("[yolo]") != std::string::npos);
	CHECK(output.find("gpt-5.5") != std::string::npos);
	CHECK(output.find("working") != std::string::npos);
	CHECK(output.find("context: 150 tokens") != std::string::npos);
}
