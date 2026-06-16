#pragma once

#include <string>

#include <ftxui/dom/elements.hpp>

namespace codeharness::tui
{

/// Simple keyword/regex-based syntax highlighter for common languages.
class CodeHighlighter
{
public:
	/// Returns an Element with syntax-highlighted code.
	static ftxui::Element Highlight(const std::string& code, const std::string& lang);
};

} // namespace codeharness::tui