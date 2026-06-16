#include "Tui/Components/QuestionDialog.h"

namespace codeharness::tui
{

ftxui::Component QuestionDialog::Create()
{
	using namespace ftxui;
	return Renderer([] { return text(""); });
}

} // namespace codeharness::tui