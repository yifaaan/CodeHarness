#include "Tui/Components/ChatPane.h"

namespace codeharness::tui
{

	ftxui::Component ChatPane::Create()
	{
		// Placeholder: the actual rendering is in TuiApp::MakeChatPane()
		// This component will be expanded in Phase 3.
		using namespace ftxui;
		return Renderer([] {
			return text("Chat") | center | flex;
		});
	}

} // namespace codeharness::tui