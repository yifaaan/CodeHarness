#include "Tui/Components/SettingsDialog.h"

namespace codeharness::tui
{

ftxui::Component SettingsDialog::Create()
{
	using namespace ftxui;
	return Renderer([] { return text(""); });
}

} // namespace codeharness::tui