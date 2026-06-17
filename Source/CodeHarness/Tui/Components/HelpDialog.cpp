#include "Tui/Components/HelpDialog.h"

namespace codeharness::tui
{

	ftxui::Component HelpDialog::Create()
	{
		using namespace ftxui;
		return Renderer([] { return text(""); });
	}

} // namespace codeharness::tui