#include "Tui/Components/SessionPicker.h"

namespace codeharness::tui
{

ftxui::Component SessionPicker::Create()
{
	using namespace ftxui;
	return Renderer([] { return text(""); });
}

} // namespace codeharness::tui