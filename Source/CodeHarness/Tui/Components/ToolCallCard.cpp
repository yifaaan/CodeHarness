#include "Tui/Components/ToolCallCard.h"

#include <string>
#include <utility>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "Tui/Renderers/DiffRenderer.h"
#include "Tui/Renderers/MarkdownRenderer.h"
#include "Tui/TuiState.h"

namespace codeharness::tui
{

namespace
{

using ftxui::Color;
using ftxui::Element;
using ftxui::Elements;
using ftxui::spinner;
using ftxui::text;

// Pick a short, useful preview from the tool args. Falls back to the first
// JSON value if none of the known field names are present.
std::string ArgsPreview(const nlohmann::json& args)
{
	std::string out;
	if (args.is_object())
	{
		for (const char* key : {"path", "file_path", "file", "command", "pattern", "query", "url"})
		{
			if (args.contains(key))
			{
				const auto& v = args[key];
				if (v.is_string())
				{
					out = v.get<std::string>();
					break;
				}
			}
		}
		if (out.empty() && !args.empty())
		{
			out = args.begin().value().dump();
		}
	}
	if (out.size() > 60)
	{
		out = out.substr(0, 57) + "...";
	}
	return out;
}

// Some tools return structured output that deserves its own renderer.
// Currently: `edit` results render as colored diffs; everything else falls
// back to markdown.
Element RenderToolOutput(const ToolCallState& tc)
{
	if (tc.name == "edit" || tc.name == "write")
	{
		// Heuristic: only treat as diff if it starts with a diff marker.
		if (!tc.output.empty() &&
			(tc.output[0] == '@' || tc.output.find("\n@@") != std::string::npos ||
			 tc.output.find("--- ") != std::string::npos))
		{
			return DiffRenderer::Render(tc.output);
		}
	}
	return MarkdownRenderer::Render(tc.output);
}

} // namespace

Element ToolCallCard::Render(const ToolCallState& tc, size_t spinnerFrame)
{
	Color statusColor;
	Element iconEl;
	if (tc.status == "running")
	{
		statusColor = Color::Yellow;
		iconEl = spinner(7, spinnerFrame) | ftxui::bold | ftxui::color(statusColor);
	}
	else if (tc.status == "error")
	{
		statusColor = Color::Red;
		iconEl = text("x") | ftxui::bold | ftxui::color(statusColor);
	}
	else
	{
		statusColor = Color::Green;
		iconEl = text("+") | ftxui::bold | ftxui::color(statusColor);
	}

	auto argsPreview = ArgsPreview(tc.args);

	Elements header;
	header.push_back(text("  "));
	header.push_back(std::move(iconEl));
	header.push_back(text(" "));
	header.push_back(text(tc.name) | ftxui::bold);
	if (!argsPreview.empty())
	{
		header.push_back(text("  ") | ftxui::dim);
		header.push_back(text(argsPreview) | ftxui::dim | ftxui::color(Color::GrayLight));
	}

	Elements cardElems;
	cardElems.push_back(ftxui::hbox(std::move(header)));

	bool showBody = (tc.status != "running" && !tc.output.empty()) || tc.expanded;
	if (showBody)
	{
		cardElems.push_back(text(""));
		cardElems.push_back(RenderToolOutput(tc) | ftxui::color(Color::GrayLight));
	}

	return ftxui::vbox(std::move(cardElems));
}

} // namespace codeharness::tui
