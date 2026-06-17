#include "Tui/Components/ToolCallCard.h"
#include "Tui/TuiState.h"

#include <doctest/doctest.h>

#include <string>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

namespace
{

	std::string RenderToString(const codeharness::tui::ToolCallState& tool,
							   int width = 120,
							   int height = 12)
	{
		ftxui::Screen screen(width, height);
		ftxui::Render(screen, codeharness::tui::ToolCallCard::Render(tool, 0));
		return screen.ToString();
	}

	codeharness::tui::ToolCallState MakeTool(std::string name)
	{
		codeharness::tui::ToolCallState tool;
		tool.id = "call_1";
		tool.name = std::move(name);
		tool.status = "done";
		return tool;
	}

} // namespace

TEST_CASE("ToolCallCard: renders status markers")
{
	auto done = MakeTool("Read");
	done.output = "one\n";
	auto doneOut = RenderToString(done);
	CHECK(doneOut.find("\xE2\x9C\x93") != std::string::npos); // U+2713 check mark.

	auto failed = MakeTool("Read");
	failed.status = "error";
	failed.output = "boom";
	auto failedOut = RenderToString(failed);
	CHECK(failedOut.find("\xE2\x9C\x97") != std::string::npos); // U+2717 ballot x.
	CHECK(failedOut.find("boom") != std::string::npos);

	auto running = MakeTool("Bash");
	running.status = "running";
	auto runningOut = RenderToString(running);
	CHECK(runningOut.find("Bash") != std::string::npos);
}

TEST_CASE("ToolCallCard: extracts key argument previews")
{
	auto bash = MakeTool("Bash");
	bash.args = {{"command", "git status"}};
	CHECK(RenderToString(bash).find("git status") != std::string::npos);

	auto read = MakeTool("Read");
	read.args = {{"file_path", "Source/CodeHarness/Tui/Components/ToolCallCard.cpp"}};
	read.output = "a\nb\n";
	CHECK(RenderToString(read).find("ToolCallCard.cpp") != std::string::npos);

	auto grep = MakeTool("Grep");
	grep.args = {{"pattern", "ToolCallCard"}};
	grep.output = "Source/a.cpp:1:ToolCallCard\n";
	CHECK(RenderToString(grep).find("ToolCallCard") != std::string::npos);
}

TEST_CASE("ToolCallCard: long path previews preserve filename tail")
{
	auto tool = MakeTool("Read");
	tool.args = {{"path", "D:/very/long/path/with/many/segments/that/should/be/truncated/important_file.cpp"}};
	tool.output = "line\n";

	auto out = RenderToString(tool);
	CHECK(out.find("important_file.cpp") != std::string::npos);
	CHECK(out.find("...") != std::string::npos);
}

TEST_CASE("ToolCallCard: grep and glob render chips and glance summaries")
{
	auto grep = MakeTool("Grep");
	grep.args = {{"pattern", "needle"}};
	grep.output = "src/a.cpp:1:needle\nsrc/b.cpp:2:needle\nsrc/c.cpp:3:needle\nsrc/d.cpp:4:needle\n";
	auto grepOut = RenderToString(grep);
	CHECK(grepOut.find("4 matches") != std::string::npos);
	CHECK(grepOut.find("src/a.cpp:1") != std::string::npos);
	CHECK(grepOut.find("+1 more") != std::string::npos);

	auto glob = MakeTool("Glob");
	glob.args = {{"pattern", "*.cpp"}};
	glob.output = "a.cpp\nb.cpp\nc.cpp\nd.cpp\n";
	auto globOut = RenderToString(glob);
	CHECK(globOut.find("4 files") != std::string::npos);
	CHECK(globOut.find("a.cpp, b.cpp, c.cpp, +1 more") != std::string::npos);
}

TEST_CASE("ToolCallCard: generic output truncates and expands")
{
	auto tool = MakeTool("mcp__server__tool");
	tool.output = "one\ntwo\nthree\nfour\nfive\n";

	auto collapsed = RenderToString(tool);
	CHECK(collapsed.find("one") != std::string::npos);
	CHECK(collapsed.find("three") != std::string::npos);
	CHECK(collapsed.find("five") == std::string::npos);
	CHECK(collapsed.find("2 more lines") != std::string::npos);

	tool.expanded = true;
	auto expanded = RenderToString(tool);
	CHECK(expanded.find("five") != std::string::npos);
}

TEST_CASE("ToolCallCard: successful quiet tools hide body unless expanded")
{
	auto tool = MakeTool("Read");
	tool.args = {{"path", "file.txt"}};
	tool.output = "first\nsecond\n";

	auto collapsed = RenderToString(tool);
	CHECK(collapsed.find("2 lines") != std::string::npos);
	CHECK(collapsed.find("first") == std::string::npos);

	tool.expanded = true;
	auto expanded = RenderToString(tool);
	CHECK(expanded.find("first") != std::string::npos);
}

TEST_CASE("ToolCallCard: write and edit chips are computed from args")
{
	auto write = MakeTool("Write");
	write.args = {{"path", "file.txt"}, {"content", "a\nb\nc\n"}};
	write.output = "ok";
	CHECK(RenderToString(write).find("3 lines") != std::string::npos);

	auto edit = MakeTool("Edit");
	edit.args = {
		{"path", "file.txt"},
		{"old_string", "keep\nold\nend"},
		{"new_string", "keep\nnew\nextra\nend"},
	};
	edit.output = "ok";
	auto out = RenderToString(edit);
	CHECK(out.find("+2 -1") != std::string::npos);
}
