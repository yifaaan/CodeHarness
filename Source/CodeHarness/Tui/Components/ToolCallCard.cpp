#include "Tui/Components/ToolCallCard.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "Tui/Renderers/DiffRenderer.h"
#include "Tui/TuiState.h"
#include "fmt/format.h"

namespace codeharness::tui
{

	namespace
	{

		using ftxui::Color;
		using ftxui::Element;
		using ftxui::Elements;
		using ftxui::spinner;
		using ftxui::text;

		constexpr size_t kMaxArgLength = 60;
		constexpr size_t kPreviewLines = 3;

		std::string Lower(std::string_view value)
		{
			std::string out(value);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
			return out;
		}

		bool ToolIs(std::string_view name, std::string_view expected)
		{
			return Lower(name) == Lower(expected);
		}

		std::optional<std::string> StringArg(const nlohmann::json& args,
											 std::initializer_list<const char*> keys)
		{
			if (!args.is_object())
			{
				return std::nullopt;
			}
			for (const char* key : keys)
			{
				if (!args.contains(key))
				{
					continue;
				}
				const auto& v = args[key];
				if (v.is_string())
				{
					auto value = v.get<std::string>();
					if (!value.empty())
					{
						return value;
					}
				}
			}
			return std::nullopt;
		}

		std::string FirstLine(std::string value)
		{
			const auto pos = value.find('\n');
			if (pos != std::string::npos)
			{
				value = value.substr(0, pos) + "...";
			}
			return value;
		}

		std::string TruncateArg(std::string value, bool preserveTail)
		{
			if (value.size() <= kMaxArgLength)
			{
				return value;
			}
			if (preserveTail)
			{
				return "..." + value.substr(value.size() - (kMaxArgLength - 3));
			}
			return value.substr(0, kMaxArgLength - 3) + "...";
		}

		// Pick a short, useful preview from the tool args. The priority list mirrors
		// Kimi's per-tool key argument extraction, with a fallback for custom/MCP tools.
		std::string ArgsPreview(std::string_view toolName, const nlohmann::json& args)
		{
			std::optional<std::string> out;
			bool preserveTail = false;

			if (ToolIs(toolName, "Bash"))
			{
				out = StringArg(args, {"command"});
			}
			else if (ToolIs(toolName, "Read") || ToolIs(toolName, "Write") || ToolIs(toolName, "Edit"))
			{
				out = StringArg(args, {"path", "file_path", "file"});
				preserveTail = true;
			}
			else if (ToolIs(toolName, "Grep") || ToolIs(toolName, "Glob"))
			{
				out = StringArg(args, {"pattern"});
			}
			else
			{
				out = StringArg(args, {"url", "query", "path", "file_path", "file", "command", "pattern"});
			}

			if (!out.has_value() && args.is_object())
			{
				for (const auto& item : args.items())
				{
					if (item.value().is_string())
					{
						out = item.value().get<std::string>();
						if (!out->empty())
						{
							break;
						}
					}
				}
			}

			if (!out.has_value())
			{
				return "";
			}
			return TruncateArg(FirstLine(*out), preserveTail);
		}

		bool IsBlank(std::string_view value)
		{
			return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
				return std::isspace(ch) != 0;
			});
		}

		std::vector<std::string> SplitLines(std::string_view textValue)
		{
			std::vector<std::string> lines;
			std::stringstream ss{std::string(textValue)};
			std::string line;
			while (std::getline(ss, line))
			{
				if (!line.empty() && line.back() == '\r')
				{
					line.pop_back();
				}
				lines.push_back(line);
			}
			if (!textValue.empty() && textValue.back() == '\n')
			{
				lines.push_back("");
			}
			while (!lines.empty() && IsBlank(lines.back()))
			{
				lines.pop_back();
			}
			return lines;
		}

		std::vector<std::string> NonEmptyLines(std::string_view textValue)
		{
			auto lines = SplitLines(textValue);
			std::vector<std::string> out;
			for (auto& line : lines)
			{
				if (!IsBlank(line))
				{
					out.push_back(std::move(line));
				}
			}
			return out;
		}

		size_t CountNonEmptyLines(std::string_view textValue)
		{
			return NonEmptyLines(textValue).size();
		}

		std::string Pluralize(size_t count, std::string_view singular, std::string_view plural = {})
		{
			std::string label;
			if (count == 1)
			{
				label = std::string(singular);
			}
			else if (plural.empty())
			{
				label = std::string(singular) + "s";
			}
			else
			{
				label = std::string(plural);
			}
			return std::to_string(count) + " " + label;
		}

		std::string FormatBytes(size_t bytes)
		{
			if (bytes < 1024)
			{
				return std::to_string(bytes) + " B";
			}
			if (bytes < 1024 * 1024)
			{
				return fmt::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
			}
			return fmt::format("{:.1f} MB", static_cast<double>(bytes) / 1024.0 / 1024.0);
		}

		size_t CountWriteLines(const nlohmann::json& args)
		{
			auto content = StringArg(args, {"content"});
			if (!content.has_value() || content->empty())
			{
				return 0;
			}
			if (content->back() == '\n')
			{
				content->pop_back();
			}
			return content->empty() ? 0 : static_cast<size_t>(std::count(content->begin(), content->end(), '\n') + 1);
		}

		std::pair<size_t, size_t> ComputeEditStats(const nlohmann::json& args)
		{
			auto oldText = StringArg(args, {"old_string"}).value_or("");
			auto newText = StringArg(args, {"new_string"}).value_or("");
			auto oldLines = SplitLines(oldText);
			auto newLines = SplitLines(newText);

			size_t prefix = 0;
			while (prefix < oldLines.size() && prefix < newLines.size() && oldLines[prefix] == newLines[prefix])
			{
				++prefix;
			}

			size_t oldSuffix = oldLines.size();
			size_t newSuffix = newLines.size();
			while (oldSuffix > prefix && newSuffix > prefix && oldLines[oldSuffix - 1] == newLines[newSuffix - 1])
			{
				--oldSuffix;
				--newSuffix;
			}

			return {newSuffix - prefix, oldSuffix - prefix};
		}

		std::string FormatEditChip(const nlohmann::json& args)
		{
			auto [added, removed] = ComputeEditStats(args);
			std::string out;
			if (added > 0)
			{
				out += "+" + std::to_string(added);
			}
			if (removed > 0)
			{
				if (!out.empty())
				{
					out += " ";
				}
				out += "-" + std::to_string(removed);
			}
			return out;
		}

		std::string ResultChip(const ToolCallState& tc)
		{
			if (tc.status == "running" || tc.status == "error")
			{
				return "";
			}
			if (ToolIs(tc.name, "Read"))
			{
				return Pluralize(CountNonEmptyLines(tc.output), "line");
			}
			if (ToolIs(tc.name, "Grep"))
			{
				const auto count = CountNonEmptyLines(tc.output);
				return count == 0 ? "no matches" : Pluralize(count, "match", "matches");
			}
			if (ToolIs(tc.name, "Glob"))
			{
				const auto count = CountNonEmptyLines(tc.output);
				return count == 0 ? "no files" : Pluralize(count, "file");
			}
			if (ToolIs(tc.name, "Write"))
			{
				return Pluralize(CountWriteLines(tc.args), "line");
			}
			if (ToolIs(tc.name, "Edit"))
			{
				return FormatEditChip(tc.args);
			}
			if (ToolIs(tc.name, "FetchURL") || ToolIs(tc.name, "FetchUrl"))
			{
				return FormatBytes(tc.output.size());
			}
			return "";
		}

		bool LooksLikeDiff(std::string_view output)
		{
			return !output.empty() &&
				   (output.front() == '@' || output.find("\n@@") != std::string_view::npos ||
					output.find("--- ") != std::string_view::npos);
		}

		Element LinesElement(const std::vector<std::string>& lines, Color color)
		{
			Elements body;
			for (const auto& line : lines)
			{
				body.push_back(text("  " + line) | ftxui::color(color));
			}
			return ftxui::vbox(std::move(body));
		}

		Element RenderTruncated(std::string_view output, bool expanded, bool isError)
		{
			auto lines = SplitLines(output);
			if (lines.empty())
			{
				return text("");
			}
			const auto color = isError ? Color::Red : Color::GrayLight;
			if (expanded || lines.size() <= kPreviewLines)
			{
				return LinesElement(lines, color);
			}

			std::vector<std::string> shown(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(kPreviewLines));
			shown.push_back(fmt::format("... ({} more lines, ctrl+o to expand)", lines.size() - kPreviewLines));
			return LinesElement(shown, color);
		}

		std::string PathFromGrepLine(const std::string& line)
		{
			const auto first = line.find(':');
			if (first == std::string::npos || first == 0)
			{
				return line;
			}
			const auto second = line.find(':', first + 1);
			if (second == std::string::npos)
			{
				return line;
			}
			return line.substr(0, second);
		}

		Element RenderGlance(const ToolCallState& tc, bool grep)
		{
			auto lines = NonEmptyLines(tc.output);
			if (lines.empty())
			{
				return text("");
			}
			std::vector<std::string> samples;
			for (size_t i = 0; i < lines.size() && i < kPreviewLines; ++i)
			{
				samples.push_back(grep ? PathFromGrepLine(lines[i]) : lines[i]);
			}
			std::string line;
			for (size_t i = 0; i < samples.size(); ++i)
			{
				if (i > 0)
				{
					line += ", ";
				}
				line += samples[i];
			}
			if (lines.size() > samples.size())
			{
				line += fmt::format(", +{} more", lines.size() - samples.size());
			}
			return text("  " + line) | ftxui::dim | ftxui::color(Color::GrayLight);
		}

		Element RenderBashBody(const ToolCallState& tc)
		{
			Elements body;
			if (tc.expanded)
			{
				if (auto command = StringArg(tc.args, {"command"}); command.has_value())
				{
					body.push_back(text("  $ " + *command) | ftxui::dim | ftxui::color(Color::GrayLight));
				}
			}
			if (!tc.output.empty())
			{
				body.push_back(RenderTruncated(tc.output, tc.expanded, tc.status == "error"));
			}
			return ftxui::vbox(std::move(body));
		}

		Element RenderToolOutput(const ToolCallState& tc)
		{
			const bool isError = tc.status == "error";
			if (isError)
			{
				return RenderTruncated(tc.error.empty() ? tc.output : tc.error, tc.expanded, true);
			}
			if (ToolIs(tc.name, "Grep"))
			{
				return tc.expanded ? RenderTruncated(tc.output, true, false) : RenderGlance(tc, true);
			}
			if (ToolIs(tc.name, "Glob"))
			{
				return tc.expanded ? RenderTruncated(tc.output, true, false) : RenderGlance(tc, false);
			}
			if (ToolIs(tc.name, "Bash"))
			{
				return RenderBashBody(tc);
			}
			if (ToolIs(tc.name, "Edit") || ToolIs(tc.name, "Write"))
			{
				if (tc.expanded && LooksLikeDiff(tc.output))
				{
					return DiffRenderer::Render(tc.output);
				}
				return tc.expanded ? RenderTruncated(tc.output, true, false) : text("");
			}
			if (ToolIs(tc.name, "Read") || ToolIs(tc.name, "FetchURL") || ToolIs(tc.name, "FetchUrl") ||
				ToolIs(tc.name, "WebSearch") || ToolIs(tc.name, "Think"))
			{
				return tc.expanded ? RenderTruncated(tc.output, true, false) : text("");
			}
			return RenderTruncated(tc.output, tc.expanded, false);
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
			iconEl = text("\xE2\x9C\x97") | ftxui::bold | ftxui::color(statusColor);
		}
		else
		{
			statusColor = Color::Green;
			iconEl = text("\xE2\x9C\x93") | ftxui::bold | ftxui::color(statusColor);
		}

		auto argsPreview = ArgsPreview(tc.name, tc.args);
		auto chip = ResultChip(tc);

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
		if (!chip.empty())
		{
			header.push_back(text("  ") | ftxui::dim);
			header.push_back(text(chip) | ftxui::dim | ftxui::color(Color::GrayLight));
		}

		Elements cardElems;
		cardElems.push_back(ftxui::hbox(std::move(header)));

		bool hiddenSuccessBody = (ToolIs(tc.name, "Read") || ToolIs(tc.name, "Write") || ToolIs(tc.name, "Edit") ||
								  ToolIs(tc.name, "FetchURL") || ToolIs(tc.name, "FetchUrl") || ToolIs(tc.name, "WebSearch") ||
								  ToolIs(tc.name, "Think")) &&
								 tc.status != "error" && !tc.expanded;
		bool showBody = !hiddenSuccessBody && ((tc.status != "running" && !tc.output.empty()) ||
											   (tc.status == "error" && (!tc.output.empty() || !tc.error.empty())) || tc.expanded);
		if (showBody)
		{
			cardElems.push_back(text(""));
			cardElems.push_back(RenderToolOutput(tc));
		}

		return ftxui::vbox(std::move(cardElems));
	}

} // namespace codeharness::tui
