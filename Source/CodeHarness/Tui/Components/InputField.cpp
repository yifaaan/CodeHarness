#include "Tui/Components/InputField.h"

namespace codeharness::tui
{

ftxui::Component InputField::Create()
{
	using namespace ftxui;
	return Input(InputOption{});
}

} // namespace codeharness::tui