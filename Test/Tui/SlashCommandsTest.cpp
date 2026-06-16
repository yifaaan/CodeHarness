#include "Tui/Utils/SlashCommands.h"

#include <doctest/doctest.h>

namespace tui = codeharness::tui;

TEST_CASE("SlashCommands: parses slash input with args")
{
	auto parsed = tui::SlashCommands::Parse("/rename my session");
	REQUIRE(parsed.has_value());
	CHECK(parsed->name == "rename");
	CHECK(parsed->args == "my session");
}

TEST_CASE("SlashCommands: matches Kimi parse behavior for path-like args")
{
	auto parsed = tui::SlashCommands::Parse("/export-md C:/tmp/session.md  ");
	REQUIRE(parsed.has_value());
	CHECK(parsed->name == "export-md");
	CHECK(parsed->args == "C:/tmp/session.md");

	CHECK_FALSE(tui::SlashCommands::Parse("/bad/name arg").has_value());
	CHECK_FALSE(tui::SlashCommands::Parse("/   ").has_value());
}

TEST_CASE("SlashCommands: accepts aliases and canonicalizes them")
{
	CHECK(tui::SlashCommands::CanonicalName("h") == "help");
	CHECK(tui::SlashCommands::CanonicalName("resume") == "sessions");
	CHECK(tui::SlashCommands::CanonicalName("q") == "exit");
	CHECK(tui::SlashCommands::CanonicalName("mode") == "yolo");
}

TEST_CASE("SlashCommands: exposes Kimi command catalog")
{
	auto commands = tui::SlashCommands::All();
	CHECK(commands.size() >= 20);
	CHECK(tui::SlashCommands::Find("btw") != nullptr);
	CHECK(tui::SlashCommands::Find("config") != nullptr);
	CHECK(tui::SlashCommands::Find("providers") != nullptr);
	CHECK(tui::SlashCommands::Find("unknown") == nullptr);
}
