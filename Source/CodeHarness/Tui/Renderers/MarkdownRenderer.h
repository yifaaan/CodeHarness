#pragma once

#include <memory>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>

namespace codeharness::tui
{

	/// Regex/line-based markdown parser that produces FTXUI Elements.
	///
	/// Supports a useful subset:
	/// - Headings (#, ##, ###)
	/// - Bold (**text** / __text__)
	/// - Italic (*text* / _text_)
	/// - Inline code (`code`)
	/// - Code fences (``` ... ```)
	/// - Unordered lists (-, *)
	/// - Ordered lists (1., 2., ...)
	/// - Blockquotes (>)
	/// - Horizontal rules (---)
	/// - Paragraphs (auto-wrapped)
	class MarkdownRenderer
	{
	public:
		/// Render a multi-line markdown string into a vertical FTXUI Element.
		static ftxui::Element Render(const std::string& text);

		/// Render a single paragraph line with inline formatting (bold/italic/code).
		/// Returns one Element per logical "span group" — useful for streaming.
		static ftxui::Element RenderInline(const std::string& text);
	};

} // namespace codeharness::tui