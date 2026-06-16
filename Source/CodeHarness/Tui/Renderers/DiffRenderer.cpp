#include "Tui/Renderers/DiffRenderer.h"

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
using ftxui::hbox;
using ftxui::text;

bool StartsWith(std::string_view s, std::string_view prefix)
{
	return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

Element RenderDiffLine(std::string_view line)
{
	// Order matters: more specific prefixes first.
	if (line.empty())
	{
		return text(" ");
	}

	// `\ No newline at end of file` warning.
	if (line[0] == '\\')
	{
		return text(std::string(line)) | ftxui::color(Color::Yellow) | ftxui::dim;
	}

	// Hunk header: `@@ -a,b +c,d @@ context`
	if (StartsWith(line, "@@"))
	{
		return hbox({
			text("@@") | ftxui::color(Color::Cyan) | ftxui::bold,
			text(std::string(line.substr(2))) | ftxui::color(Color::Cyan),
		});
	}

	// Diff metadata: `diff --git`, `index abc..def`, `--- a/`, `+++ b/`
	if (StartsWith(line, "diff ") ||
		StartsWith(line, "index ") ||
		StartsWith(line, "--- ") ||
		StartsWith(line, "+++ ") ||
		StartsWith(line, "old mode ") ||
		StartsWith(line, "new mode ") ||
		StartsWith(line, "similarity index ") ||
		StartsWith(line, "rename from ") ||
		StartsWith(line, "rename to ") ||
		StartsWith(line, "copy from ") ||
		StartsWith(line, "copy to "))
	{
		return text(std::string(line)) | ftxui::color(Color::Blue) | ftxui::bold;
	}

	// Added line `+...` but not `++` (which would be the `+++` file header).
	if (line[0] == '+')
	{
		return hbox({
			text("+") | ftxui::color(Color::Green) | ftxui::bold,
			text(std::string(line.substr(1))) | ftxui::color(Color::Green),
		});
	}

	// Removed line `-...` but not `--`.
	if (line[0] == '-')
	{
		return hbox({
			text("-") | ftxui::color(Color::Red) | ftxui::bold,
			text(std::string(line.substr(1))) | ftxui::color(Color::Red),
		});
	}

	// Context line ` ...` (leading space) or any other line.
	return text(std::string(line));
}

} // namespace

Element DiffRenderer::Render(const std::string& diff)
{
	Elements lines;
	size_t start = 0;
	while (true)
	{
		auto pos = diff.find('\n', start);
		std::string_view slice;
		if (pos == std::string::npos)
		{
			slice = std::string_view(diff).substr(start);
		}
		else
		{
			slice = std::string_view(diff).substr(start, pos - start);
		}
		lines.push_back(RenderDiffLine(slice));
		if (pos == std::string::npos) break;
		start = pos + 1;
	}

	if (lines.empty()) return text("");
	if (lines.size() == 1) return std::move(lines[0]);
	return ftxui::vbox(std::move(lines));
}

} // namespace codeharness::tui
