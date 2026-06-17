#include "Tui/Components/MessageEntry.h"

#include <doctest/doctest.h>

#include <string>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

namespace
{

	std::string RenderToString(const ftxui::Element& element, int width = 80, int height = 8)
	{
		ftxui::Screen screen(width, height);
		ftxui::Render(screen, element);
		return screen.ToString();
	}

} // namespace

TEST_CASE("MessageEntry: renders user marker and text")
{
	auto rendered = RenderToString(codeharness::tui::MessageEntry::RenderUser("hello"));
	CHECK(rendered.find("hello") != std::string::npos);
	CHECK(rendered.find("\xE2\x9C\xA8") != std::string::npos);
}

TEST_CASE("MessageEntry: renders assistant marker and markdown")
{
	auto rendered = RenderToString(codeharness::tui::MessageEntry::RenderAssistant("**bold**"));
	CHECK(rendered.find("bold") != std::string::npos);
	CHECK(rendered.find("\xE2\x97\x8F") != std::string::npos);
}

TEST_CASE("MessageEntry: assistant can suppress marker")
{
	auto rendered = RenderToString(codeharness::tui::MessageEntry::RenderAssistant("plain", false));
	CHECK(rendered.find("plain") != std::string::npos);
	CHECK(rendered.find("\xE2\x97\x8F") == std::string::npos);
}

TEST_CASE("MessageEntry: renders system and error messages")
{
	CHECK_FALSE(codeharness::tui::MessageEntry::IsErrorSystemMessage("ok"));
	CHECK(codeharness::tui::MessageEntry::IsErrorSystemMessage("Error: boom"));
	CHECK(codeharness::tui::MessageEntry::IsErrorSystemMessage("permission denied"));
	CHECK(codeharness::tui::MessageEntry::IsErrorSystemMessage("task failed"));

	auto rendered = RenderToString(codeharness::tui::MessageEntry::RenderSystem("status ready"));
	CHECK(rendered.find("status ready") != std::string::npos);

	auto errored = RenderToString(codeharness::tui::MessageEntry::RenderError("Error: boom"));
	CHECK(errored.find("Error: boom") != std::string::npos);
	CHECK(errored.find("\xE2\x9C\x97") != std::string::npos);
}
