#include "Tui/Renderers/MarkdownRenderer.h"

#include "Tui/Renderers/CodeHighlighter.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/dom/elements.hpp>

namespace codeharness::tui
{

	namespace
	{

		using ftxui::Color;
		using ftxui::Element;
		using ftxui::Elements;

		const Color kHeading1 = Color::Cyan;
		const Color kHeading2 = Color::Cyan;
		const Color kHeading3 = Color::CyanLight;
		const Color kCodeFg = Color::BlueLight;
		const Color kQuoteFg = Color::GrayLight;
		const Color kLinkFg = Color::Blue;
		const Color kListBullet = Color::Yellow;
		const Color kRuleColor = Color::GrayDark;

		// Trim leading/trailing whitespace from a string view.
		std::string_view Trim(std::string_view s)
		{
			auto begin = s.find_first_not_of(" \t");
			if (begin == std::string_view::npos)
				return {};
			auto end = s.find_last_not_of(" \t\r\n");
			return s.substr(begin, end - begin + 1);
		}

		// Split a string by single character.
		std::vector<std::string_view> Split(std::string_view s, char delim)
		{
			std::vector<std::string_view> out;
			size_t start = 0;
			while (true)
			{
				auto pos = s.find(delim, start);
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

		// Render inline markdown: parse **bold**, *italic*, `code`, and plain text.
		// Returns a horizontal box of styled text spans.
		Element RenderInlineSpan(std::string_view text)
		{
			Elements spans;
			std::string buffer;

			auto flushBuffer = [&] {
				if (!buffer.empty())
				{
					spans.push_back(ftxui::text(buffer));
					buffer.clear();
				}
			};

			size_t i = 0;
			while (i < text.size())
			{
				// Bold: **text** or __text__
				if (i + 1 < text.size() &&
					((text[i] == '*' && text[i + 1] == '*') ||
					 (text[i] == '_' && text[i + 1] == '_')))
				{
					char marker = text[i];
					size_t end = text.find(std::string_view(&marker, 1).data(), i + 2);
					// Look for the closing pair
					size_t close = std::string::npos;
					for (size_t j = i + 2; j + 1 < text.size(); ++j)
					{
						if (text[j] == marker && text[j + 1] == marker)
						{
							close = j;
							break;
						}
					}
					if (close != std::string::npos)
					{
						flushBuffer();
						auto inner = text.substr(i + 2, close - (i + 2));
						spans.push_back(ftxui::text(std::string(inner)) | ftxui::bold);
						i = close + 2;
						continue;
					}
				}

				// Inline code: `code`
				if (text[i] == '`')
				{
					size_t close = text.find('`', i + 1);
					if (close != std::string::npos)
					{
						flushBuffer();
						auto inner = text.substr(i + 1, close - (i)-1);
						spans.push_back(ftxui::text(std::string(inner)) | ftxui::color(kCodeFg));
						i = close + 1;
						continue;
					}
				}

				// Italic: *text* or _text_
				if ((text[i] == '*' || text[i] == '_') &&
					(i == 0 || text[i - 1] == ' '))
				{
					char marker = text[i];
					size_t close = std::string::npos;
					for (size_t j = i + 1; j < text.size(); ++j)
					{
						if (text[j] == marker && (j + 1 >= text.size() || text[j + 1] == ' ' ||
												  text[j + 1] == '.' || text[j + 1] == ',' ||
												  text[j + 1] == '!' || text[j + 1] == '?'))
						{
							close = j;
							break;
						}
					}
					if (close != std::string::npos && close > i + 1)
					{
						flushBuffer();
						auto inner = text.substr(i + 1, close - i - 1);
						spans.push_back(ftxui::text(std::string(inner)) | ftxui::italic);
						i = close + 1;
						continue;
					}
				}

				buffer.push_back(text[i]);
				++i;
			}

			flushBuffer();

			if (spans.empty())
			{
				return ftxui::text("");
			}
			if (spans.size() == 1)
			{
				return std::move(spans[0]);
			}
			return ftxui::hbox(std::move(spans));
		}

		// Render an unordered list item ("- text" or "* text").
		Element RenderListItem(std::string_view line)
		{
			auto content = Trim(line.substr(1));
			auto bullet = ftxui::text("  * ") | ftxui::color(kListBullet);
			return ftxui::hbox({bullet, RenderInlineSpan(content)});
		}

		// Render an ordered list item ("1. text", "2. text").
		Element RenderOrderedItem(std::string_view line)
		{
			auto dot = line.find('.');
			if (dot == std::string_view::npos)
			{
				return ftxui::text(std::string(line));
			}
			auto prefix = line.substr(0, dot + 1);
			auto content = Trim(line.substr(dot + 1));
			return ftxui::hbox({
				ftxui::text(std::string(prefix)) | ftxui::dim | ftxui::color(kListBullet),
				ftxui::text(" "),
				RenderInlineSpan(content),
			});
		}

		// Render a blockquote line ("> text").
		Element RenderQuote(std::string_view line)
		{
			auto content = Trim(line.substr(1));
			return ftxui::hbox({
				ftxui::text("  | ") | ftxui::color(kRuleColor),
				RenderInlineSpan(content) | ftxui::dim | ftxui::color(kQuoteFg),
			});
		}

		// Render a fenced code block.
		Element RenderCodeBlock(const std::vector<std::string>& lines, const std::string& lang)
		{
			std::string joined;
			for (size_t i = 0; i < lines.size(); ++i)
			{
				if (i)
					joined.push_back('\n');
				joined += lines[i];
			}
			auto body = CodeHighlighter::Highlight(joined, lang);
			return std::move(body) | ftxui::borderLight | ftxui::color(kCodeFg);
		}

	} // namespace

	Element MarkdownRenderer::Render(const std::string& text)
	{
		Elements result;
		std::vector<std::string> codeBuffer;
		bool inCodeBlock = false;
		std::string codeLang;

		auto lines = Split(text, '\n');

		for (size_t i = 0; i < lines.size(); ++i)
		{
			auto line = lines[i];
			auto trimmed = Trim(line);

			// Code fence toggle
			if (trimmed.size() >= 3 && trimmed.substr(0, 3) == "```")
			{
				if (!inCodeBlock)
				{
					inCodeBlock = true;
					codeLang = std::string(Trim(trimmed.substr(3)));
					codeBuffer.clear();
				}
				else
				{
					result.push_back(RenderCodeBlock(codeBuffer, codeLang));
					inCodeBlock = false;
					codeBuffer.clear();
				}
				continue;
			}

			if (inCodeBlock)
			{
				// Preserve original whitespace inside code blocks
				codeBuffer.push_back(std::string(line));
				continue;
			}

			// Skip empty lines (paragraph breaks)
			if (trimmed.empty())
			{
				continue;
			}

			// Horizontal rule
			if (trimmed == "---" || trimmed == "***" || trimmed == "___")
			{
				result.push_back(ftxui::separatorLight() | ftxui::color(kRuleColor));
				continue;
			}

			// Headings
			if (trimmed.size() >= 2 && trimmed[0] == '#' && trimmed[1] == ' ')
			{
				result.push_back(RenderInlineSpan(trimmed.substr(2)) | ftxui::bold | ftxui::color(kHeading1));
				continue;
			}
			if (trimmed.size() >= 3 && trimmed[0] == '#' && trimmed[1] == '#' && trimmed[2] == ' ')
			{
				result.push_back(RenderInlineSpan(trimmed.substr(3)) | ftxui::bold | ftxui::color(kHeading2));
				continue;
			}
			if (trimmed.size() >= 4 && trimmed[0] == '#' && trimmed[1] == '#' &&
				trimmed[2] == '#' && trimmed[3] == ' ')
			{
				result.push_back(RenderInlineSpan(trimmed.substr(4)) | ftxui::bold | ftxui::color(kHeading3));
				continue;
			}
			if (!trimmed.empty() && trimmed[0] == '#')
			{
				// Heading levels 4-6 - just bold
				auto space = trimmed.find(' ');
				if (space != std::string_view::npos)
				{
					result.push_back(RenderInlineSpan(trimmed.substr(space + 1)) | ftxui::bold);
					continue;
				}
			}

			// Blockquote
			if (!trimmed.empty() && trimmed[0] == '>')
			{
				result.push_back(RenderQuote(trimmed));
				continue;
			}

			// Unordered list
			if (trimmed.size() >= 2 && (trimmed[0] == '-' || trimmed[0] == '*') && trimmed[1] == ' ')
			{
				result.push_back(RenderListItem(trimmed));
				continue;
			}

			// Ordered list (digit(s) followed by '. ')
			if (!trimmed.empty() && std::isdigit(static_cast<unsigned char>(trimmed[0])))
			{
				size_t j = 0;
				while (j < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[j])))
					++j;
				if (j + 1 < trimmed.size() && trimmed[j] == '.' && trimmed[j + 1] == ' ')
				{
					result.push_back(RenderOrderedItem(trimmed));
					continue;
				}
			}

			// Plain paragraph
			result.push_back(RenderInlineSpan(trimmed));
		}

		// Unterminated code fence — render as a block too
		if (inCodeBlock && !codeBuffer.empty())
		{
			result.push_back(RenderCodeBlock(codeBuffer, codeLang));
		}

		if (result.empty())
		{
			return ftxui::text("");
		}
		if (result.size() == 1)
		{
			return std::move(result[0]);
		}
		return ftxui::vbox(std::move(result));
	}

	Element MarkdownRenderer::RenderInline(const std::string& text)
	{
		return tui::RenderInlineSpan(std::string_view(text));
	}

} // namespace codeharness::tui