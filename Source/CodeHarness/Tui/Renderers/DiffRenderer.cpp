#include "Tui/Renderers/DiffRenderer.h"

#include <ftxui/dom/elements.hpp>

namespace codeharness::tui
{

ftxui::Element DiffRenderer::Render(const std::string& diff)
{
	return ftxui::text(diff);
}

} // namespace codeharness::tui