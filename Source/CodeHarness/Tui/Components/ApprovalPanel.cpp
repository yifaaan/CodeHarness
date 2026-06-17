#include "Tui/Components/ApprovalPanel.h"

namespace codeharness::tui
{

	ftxui::Component ApprovalPanel::Create()
	{
		using namespace ftxui;
		return Renderer([] { return text(""); });
	}

} // namespace codeharness::tui