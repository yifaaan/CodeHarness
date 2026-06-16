#include "Tui/Components/ToolCallCard.h"

namespace codeharness::tui
{

ftxui::Component ToolCallCard::Create()
{
	using namespace ftxui;
	return Renderer([] { return text(""); });
}

} // namespace codeharness::tui