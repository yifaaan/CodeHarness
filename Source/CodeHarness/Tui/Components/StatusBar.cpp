#include "Tui/Components/StatusBar.h"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

namespace codeharness::tui
{

ftxui::Component StatusBar::Create()
{
	using namespace ftxui;
	return Renderer([] {
		return text("") | size(HEIGHT, EQUAL, 1);
	});
}

} // namespace codeharness::tui