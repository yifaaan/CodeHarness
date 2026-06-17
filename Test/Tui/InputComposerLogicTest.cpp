#include "Tui/Utils/InputComposerLogic.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

struct TmpDir
{
	std::filesystem::path path;

	TmpDir()
	{
		path = std::filesystem::temp_directory_path() / ("codeharness_input_composer_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(path);
	}

	~TmpDir()
	{
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}
};

} // namespace

TEST_CASE("InputComposerLogic: slash suggestions filter commands and show aliases")
{
	auto suggestions = codeharness::tui::BuildSlashSuggestions("/he");
	REQUIRE_FALSE(suggestions.empty());
	CHECK(suggestions.front().command.name == "help");
	CHECK(suggestions.front().display.find("/h") != std::string::npos);
	CHECK(suggestions.front().display.find("Show available commands") != std::string::npos);

	CHECK(codeharness::tui::BuildSlashSuggestions("hello /he").empty());
	CHECK(codeharness::tui::BuildSlashSuggestions("/help now").empty());
}

TEST_CASE("InputComposerLogic: file mention query parses active prefix")
{
	auto query = codeharness::tui::FindFileMentionQuery("open @src/mai", 13);
	REQUIRE(query.has_value());
	CHECK(query->at == 5);
	CHECK(query->prefix == "src/mai");

	CHECK_FALSE(codeharness::tui::FindFileMentionQuery("email@example.com", 17).has_value());
	CHECK_FALSE(codeharness::tui::FindFileMentionQuery("open @src/main now", 18).has_value());
}

TEST_CASE("InputComposerLogic: file mention suggestions are bounded and skip hidden entries")
{
	TmpDir dir;
	std::ofstream(dir.path / "main.cpp") << "int main(){}";
	std::ofstream(dir.path / ".hidden") << "secret";
	std::filesystem::create_directories(dir.path / "module");

	auto suggestions = codeharness::tui::BuildFileMentionSuggestions(dir.path.string(), "m", 8);
	REQUIRE(suggestions.size() == 2);
	CHECK(suggestions[0].display == "@module/");
	CHECK(suggestions[0].isDirectory);
	CHECK(suggestions[1].display == "@main.cpp");
}

TEST_CASE("InputComposerLogic: file mention completion replaces only the active token")
{
	auto query = codeharness::tui::FindFileMentionQuery("read @ma", 8);
	REQUIRE(query.has_value());
	CHECK(codeharness::tui::ApplyFileMentionCompletion("read @ma", *query, "main.cpp") == "read @main.cpp");
}

TEST_CASE("InputComposerLogic: history navigation skips multiline input")
{
	std::vector<std::string> entries = {"one", "two"};
	codeharness::tui::HistoryNavigationState nav{.cursor = entries.size()};
	std::string input = "draft";

	CHECK(codeharness::tui::ApplyHistoryUp(entries, nav, input));
	CHECK(input == "two");
	CHECK(codeharness::tui::ApplyHistoryUp(entries, nav, input));
	CHECK(input == "one");
	CHECK(codeharness::tui::ApplyHistoryDown(entries, nav, input));
	CHECK(input == "two");
	CHECK(codeharness::tui::ApplyHistoryDown(entries, nav, input));
	CHECK(input == "draft");

	input = "line1\nline2";
	CHECK_FALSE(codeharness::tui::ApplyHistoryUp(entries, nav, input));
}

TEST_CASE("InputComposerLogic: submit action handles multiline shortcut")
{
	CHECK(codeharness::tui::SubmitAction(false, false) == codeharness::tui::ComposerSubmitAction::Submit);
	CHECK(codeharness::tui::SubmitAction(true, false) == codeharness::tui::ComposerSubmitAction::InsertNewline);
	CHECK(codeharness::tui::SubmitAction(false, true) == codeharness::tui::ComposerSubmitAction::None);
	CHECK(codeharness::tui::CanUseHistoryForInput("one line"));
	CHECK_FALSE(codeharness::tui::CanUseHistoryForInput("two\nlines"));
}

TEST_CASE("InputComposerLogic: shift enter sequences are distinct from plain enter")
{
	CHECK(codeharness::tui::IsShiftEnterInputSequence("\x1b[13;2u"));
	CHECK(codeharness::tui::IsShiftEnterInputSequence("\x1b[13;2~"));
	CHECK(codeharness::tui::IsShiftEnterInputSequence("\x1b[27;2;13~"));
	CHECK_FALSE(codeharness::tui::IsShiftEnterInputSequence("\n"));
	CHECK_FALSE(codeharness::tui::IsShiftEnterInputSequence("\r"));
}
