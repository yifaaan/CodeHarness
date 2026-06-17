#include "Tui/Components/ThinkingView.h"

#include <algorithm>
#include <cctype>
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

		constexpr size_t kPreviewLines = 3;
		constexpr std::string_view kStatusBullet = "\xE2\x97\x8F"; // U+25CF.

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

		bool IsBlank(std::string_view value)
		{
			return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
				return std::isspace(ch) != 0;
			});
		}

	} // namespace

	Element ThinkingView::Render(const std::string& thinking, bool expanded)
	{
		if (thinking.empty())
		{
			return ftxui::text("");
		}

		auto lines = SplitLines(thinking);
		while (!lines.empty() && IsBlank(lines.back()))
		{
			lines.pop_back();
		}

		const bool truncated = !expanded && lines.size() > kPreviewLines;
		const size_t shownCount = truncated ? kPreviewLines : lines.size();

		Elements rows;
		for (size_t i = 0; i < shownCount; ++i)
		{
			rows.push_back(ftxui::hbox({
				text(i == 0 ? std::string(kStatusBullet) + " " : "  ") | ftxui::dim | ftxui::color(Color::GrayLight),
				text(std::string(lines[i])) | ftxui::dim | ftxui::color(Color::GrayLight),
			}));
		}
		if (truncated)
		{
			rows.push_back(ftxui::hbox({
				text("  "),
				text("... (" + std::to_string(lines.size() - kPreviewLines) + " more lines, ctrl+o to expand)") |
					ftxui::dim | ftxui::color(Color::GrayLight),
			}));
		}
		return ftxui::vbox(std::move(rows));
	}

} // namespace codeharness::tui
