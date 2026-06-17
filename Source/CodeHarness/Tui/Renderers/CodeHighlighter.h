#pragma once

#include <string>

#include <ftxui/dom/elements.hpp>

namespace codeharness::tui
{

	/// Lightweight tokenizer-based syntax highlighter for fenced code blocks.
	///
	/// Supports a useful subset of token classes (keyword / string / comment /
	/// number / punctuation / plain) across a handful of C-like languages plus
	/// bash and json. Anything unrecognized is rendered as plain monospace text.
	class CodeHighlighter
	{
	public:
		/// Returns a vbox of per-line elements, each colored by token class.
		/// Lines preserve their original spacing. If `lang` is unknown the code
		/// is rendered as a single-color block (no per-token styling).
		static ftxui::Element Highlight(const std::string& code, const std::string& lang);
	};

} // namespace codeharness::tui
