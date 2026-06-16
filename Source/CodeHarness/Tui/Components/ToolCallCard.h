#pragma once

#include <cstddef>
#include <ftxui/dom/elements.hpp>

namespace codeharness::tui
{

struct ToolCallState;

/// Renders a single ToolCall card for the transcript: status icon, tool name,
/// args preview, and (when completed/expanded) the body output. Mirrors the
/// inline rendering previously baked into TuiApp::MakeChatPane.
class ToolCallCard
{
public:
	static ftxui::Element Render(const ToolCallState& tc, size_t spinnerFrame);
};

} // namespace codeharness::tui
