#include "Tui/Renderers/MarkdownRenderer.h"

#include <doctest/doctest.h>

#include <string>

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

namespace
{

// Render an Element to a fixed-width screen and return the rendered string.
// Used to verify the Element tree contains expected substrings.
std::string RenderToString(const ftxui::Element& el, int width = 40)
{
	ftxui::Screen screen(width, 5);
	ftxui::Render(screen, el);
	return screen.ToString();
}

} // namespace

TEST_CASE("MarkdownRenderer: empty input")
{
	auto el = codeharness::tui::MarkdownRenderer::Render("");
	CHECK(el.get() != nullptr);
}

TEST_CASE("MarkdownRenderer: plain paragraph")
{
	auto el = codeharness::tui::MarkdownRenderer::Render("Hello world");
	auto str = RenderToString(el);
	CHECK(str.find("Hello world") != std::string::npos);
}

TEST_CASE("MarkdownRenderer: heading levels")
{
	auto h1 = codeharness::tui::MarkdownRenderer::Render("# Title");
	auto str = RenderToString(h1);
	CHECK(str.find("Title") != std::string::npos);

	auto h2 = codeharness::tui::MarkdownRenderer::Render("## Section");
	CHECK(RenderToString(h2).find("Section") != std::string::npos);

	auto h3 = codeharness::tui::MarkdownRenderer::Render("### Subsection");
	CHECK(RenderToString(h3).find("Subsection") != std::string::npos);
}

TEST_CASE("MarkdownRenderer: bold and italic and code")
{
	auto el = codeharness::tui::MarkdownRenderer::Render("This is **bold** and *italic* and `code`");
	auto str = RenderToString(el);
	CHECK(str.find("bold") != std::string::npos);
	CHECK(str.find("italic") != std::string::npos);
	CHECK(str.find("code") != std::string::npos);
}

TEST_CASE("MarkdownRenderer: unordered list")
{
	auto el = codeharness::tui::MarkdownRenderer::Render("- one\n- two\n- three");
	auto str = RenderToString(el);
	CHECK(str.find("one") != std::string::npos);
	CHECK(str.find("two") != std::string::npos);
	CHECK(str.find("three") != std::string::npos);
}

TEST_CASE("MarkdownRenderer: ordered list")
{
	auto el = codeharness::tui::MarkdownRenderer::Render("1. first\n2. second");
	auto str = RenderToString(el);
	CHECK(str.find("first") != std::string::npos);
	CHECK(str.find("second") != std::string::npos);
}

TEST_CASE("MarkdownRenderer: blockquote")
{
	auto el = codeharness::tui::MarkdownRenderer::Render("> quoted text");
	auto str = RenderToString(el);
	CHECK(str.find("quoted text") != std::string::npos);
}

TEST_CASE("MarkdownRenderer: code fence")
{
	auto el = codeharness::tui::MarkdownRenderer::Render("```cpp\nint main() { return 0; }\n```");
	auto str = RenderToString(el, 50);
	CHECK(str.find("int main()") != std::string::npos);
}

TEST_CASE("MarkdownRenderer: horizontal rule")
{
	auto el = codeharness::tui::MarkdownRenderer::Render("before\n\n---\n\nafter");
	auto str = RenderToString(el);
	CHECK(str.find("before") != std::string::npos);
	CHECK(str.find("after") != std::string::npos);
}

TEST_CASE("MarkdownRenderer: multiline paragraph")
{
	auto el = codeharness::tui::MarkdownRenderer::Render("line one\nline two");
	auto str = RenderToString(el);
	CHECK(str.find("line one") != std::string::npos);
	CHECK(str.find("line two") != std::string::npos);
}

TEST_CASE("MarkdownRenderer: unterminated code fence")
{
	auto el = codeharness::tui::MarkdownRenderer::Render("```\nint x = 1;");
	auto str = RenderToString(el);
	CHECK(str.find("int x = 1;") != std::string::npos);
}