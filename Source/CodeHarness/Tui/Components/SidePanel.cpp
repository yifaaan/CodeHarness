#include "Tui/Components/SidePanel.h"

namespace codeharness::tui
{

ftxui::Component SidePanel::Create()
{
	using namespace ftxui;
	return Renderer([] { return text(""); });
}

} // namespace codeharness::tui