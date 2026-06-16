#include "Tui/Components/MessageEntry.h"

namespace codeharness::tui
{

ftxui::Component MessageEntry::Create()
{
	using namespace ftxui;
	return Renderer([] { return text(""); });
}

} // namespace codeharness::tui