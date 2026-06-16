#include "Tui/Components/CompactionIndicator.h"

namespace codeharness::tui
{

ftxui::Component CompactionIndicator::Create()
{
	using namespace ftxui;
	return Renderer([] { return text(""); });
}

} // namespace codeharness::tui