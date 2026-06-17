#include "Tui/Components/ApprovalPanel.h"
#include "Tui/Components/HelpDialog.h"
#include "Tui/Components/QuestionDialog.h"
#include "Tui/Components/SessionPicker.h"
#include "Tui/Components/SettingsDialog.h"

#include <doctest/doctest.h>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

namespace
{
	std::string RenderComponent(ftxui::Component component, int width = 120, int height = 30)
	{
		ftxui::Screen screen(width, height);
		ftxui::Render(screen, component->Render());
		return screen.ToString();
	}

	codeharness::tui::ApprovalPanelRequest MakeApprovalRequest()
	{
		return codeharness::tui::ApprovalPanelRequest{
			.toolName = "Bash",
			.args = {{"command", "git status"}},
			.description = "Run git status",
		};
	}

	std::int64_t NowMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::system_clock::now().time_since_epoch())
			.count();
	}
}

TEST_CASE("ApprovalPanel: numeric choices drive decisions and legacy keys do not")
{
	std::optional<codeharness::tui::ApprovalPanelResponse> response;
	auto component = codeharness::tui::ApprovalPanel::Create({
		.request = [] { return std::optional<codeharness::tui::ApprovalPanelRequest>{MakeApprovalRequest()}; },
		.onResponse = [&](codeharness::tui::ApprovalPanelResponse r) { response = std::move(r); },
	});

	CHECK_FALSE(component->OnEvent(ftxui::Event::Character('a')));
	CHECK_FALSE(response.has_value());
	CHECK(component->OnEvent(ftxui::Event::Character('2')));
	REQUIRE(response.has_value());
	CHECK(response->decision == codeharness::permission::PermissionDecision::Allow);
	CHECK(response->selectedLabel == "Approve for this session");
}

TEST_CASE("ApprovalPanel: reject with feedback and ctrl-o forwarding")
{
	int toggles = 0;
	std::optional<codeharness::tui::ApprovalPanelResponse> response;
	auto component = codeharness::tui::ApprovalPanel::Create({
		.request = [] { return std::optional<codeharness::tui::ApprovalPanelRequest>{MakeApprovalRequest()}; },
		.onResponse = [&](codeharness::tui::ApprovalPanelResponse r) { response = std::move(r); },
		.onToggleToolOutput = [&] { ++toggles; },
	});

	CHECK(component->OnEvent(ftxui::Event::CtrlO));
	CHECK(toggles == 1);
	CHECK(component->OnEvent(ftxui::Event::Character('4')));
	CHECK(component->OnEvent(ftxui::Event::Character('n')));
	CHECK(component->OnEvent(ftxui::Event::Character('o')));
	CHECK(component->OnEvent(ftxui::Event::Return));
	REQUIRE(response.has_value());
	CHECK(response->decision == codeharness::permission::PermissionDecision::Deny);
	CHECK(response->feedback == "no");
}

TEST_CASE("HelpDialog: closes and scrolls command window")
{
	bool closed = false;
	std::vector<codeharness::tui::SlashCommands::Command> commands;
	for (int i = 0; i < 12; ++i)
	{
		commands.push_back({.name = "cmd" + std::to_string(i), .description = "desc"});
	}
	auto component = codeharness::tui::HelpDialog::Create({.commands = commands, .onClose = [&] { closed = true; }, .maxVisible = 6});

	CHECK(RenderComponent(component).find("showing 1-6") != std::string::npos);
	CHECK(component->OnEvent(ftxui::Event::ArrowDown));
	CHECK(RenderComponent(component).find("showing 2-7") != std::string::npos);
	CHECK(component->OnEvent(ftxui::Event::q));
	CHECK(closed);
}

TEST_CASE("SessionPicker: renders session details, searches, and selects")
{
	std::vector<codeharness::session::SessionInfo> sessions = {
		{.sessionId = "ses_alpha_full", .title = "Alpha", .workdir = "D:/code/alpha", .updatedAt = NowMs() - 2 * 60 * 1000},
		{.sessionId = "ses_beta_full", .title = "Beta", .workdir = "D:/code/beta", .updatedAt = NowMs() - 3 * 60 * 60 * 1000},
	};
	std::optional<std::string> selected;
	bool toggledScope = false;
	auto component = codeharness::tui::SessionPicker::Create({
		.sessions = [&] { return sessions; },
		.currentSessionId = [] { return std::string("ses_alpha_full"); },
		.scope = [] { return codeharness::tui::SessionPickerScope::Cwd; },
		.onSelect = [&](codeharness::session::SessionInfo info) { selected = info.sessionId; },
		.onToggleScope = [&] { toggledScope = true; },
	});

	auto out = RenderComponent(component);
	CHECK(out.find("Alpha") != std::string::npos);
	CHECK(out.find("ses_alpha_full") != std::string::npos);
	CHECK(out.find("current") != std::string::npos);
	CHECK(component->OnEvent(ftxui::Event::Character('B')));
	CHECK(RenderComponent(component).find("Alpha") == std::string::npos);
	CHECK(component->OnEvent(ftxui::Event::Return));
	REQUIRE(selected.has_value());
	CHECK(*selected == "ses_beta_full");
	CHECK(component->OnEvent(ftxui::Event::CtrlA));
	CHECK(toggledScope);
}

TEST_CASE("SettingsDialog: renders top-level settings and selects entries")
{
	std::optional<codeharness::tui::SettingsSelection> selection;
	bool cancelled = false;
	auto component = codeharness::tui::SettingsDialog::Create({
		.currentModel = [] { return std::string("gpt-5.5"); },
		.currentPermissionMode = [] { return codeharness::config::PermissionMode::Manual; },
		.onSelect = [&](codeharness::tui::SettingsSelection s) { selection = s; },
		.onCancel = [&] { cancelled = true; },
	});

	auto out = RenderComponent(component);
	CHECK(out.find("Model") != std::string::npos);
	CHECK(out.find("Switch the active model") != std::string::npos);
	CHECK(component->OnEvent(ftxui::Event::Return));
	REQUIRE(selection.has_value());
	CHECK(*selection == codeharness::tui::SettingsSelection::Model);
	CHECK(component->OnEvent(ftxui::Event::Escape));
	CHECK(cancelled);
}

TEST_CASE("QuestionDialog: numeric choice, freeform, and cancel")
{
	codeharness::tools::QuestionRequest request{
		.question = "Pick one?",
		.options = {"A", "B"},
		.allowFreeform = true,
	};
	std::optional<std::string> answer;
	auto component = codeharness::tui::QuestionDialog::Create({
		.request = [&] { return std::optional<codeharness::tools::QuestionRequest>{request}; },
		.onAnswer = [&](std::string a) { answer = std::move(a); },
	});

	CHECK(component->OnEvent(ftxui::Event::Character('2')));
	REQUIRE(answer.has_value());
	CHECK(*answer == "B");

	answer.reset();
	CHECK(component->OnEvent(ftxui::Event::Character('3')));
	CHECK(component->OnEvent(ftxui::Event::Character('h')));
	CHECK(component->OnEvent(ftxui::Event::Character('i')));
	CHECK(component->OnEvent(ftxui::Event::Return));
	REQUIRE(answer.has_value());
	CHECK(*answer == "hi");

	answer.reset();
	CHECK(component->OnEvent(ftxui::Event::Escape));
	REQUIRE(answer.has_value());
	CHECK(answer->empty());
}
