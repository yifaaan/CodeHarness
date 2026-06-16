#include "Tui/Components/ThinkingView.h"

#include <string>
#include <string_view>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

namespace codeharness::tui
{

namespace
{

using ftxui::Color;
using ftxui::Element;
using ftxui::Elements;
using ftxui::text;

std::vector<std::string_view> SplitLines(std::string_view s)
{
	std::vector<std::string_view> out;
	size_t start = 0;
	while (true)
	{
		auto pos = s.find('\n', start);
		if (pos == std::string_view::npos)
		{
			out.push_back(s.substr(start));
			break;
		}
		out.push_back(s.substr(start, pos - start));
		start = pos + 1;
	}
	return out;
}

} // namespace

Element ThinkingView::Render(const std::string& thinking)
{
	if (thinking.empty())
	{
		return ftxui::text("");
	}

	Elements rows;
	rows.push_back(text(" 💭 thinking") | ftxui::bold | ftxui::color(Color::Magenta));
	for (auto line : SplitLines(thinking))
	{
		rows.push_back(ftxui::hbox({
			text("     "),
			text(std::string(line)) | ftxui::dim | ftxui::color(Color::GrayLight),
		}));
	}
	return ftxui::vbox(std::move(rows));
}

} // namespace codeharness::tui
