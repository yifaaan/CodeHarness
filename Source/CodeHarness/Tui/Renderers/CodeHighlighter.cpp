#include "Tui/Renderers/CodeHighlighter.h"

#include <ftxui/dom/elements.hpp>

namespace codeharness::tui
{

ftxui::Element CodeHighlighter::Highlight(const std::string& code, const std::string& /*lang*/)
{
	return ftxui::text(code);
}

} // namespace codeharness::tui