#include "Tui/Components/Spinner.h"

namespace codeharness::tui
{

	ftxui::Component Spinner::Create()
	{
		using namespace ftxui;
		return Renderer([] { return text(""); });
	}

} // namespace codeharness::tui