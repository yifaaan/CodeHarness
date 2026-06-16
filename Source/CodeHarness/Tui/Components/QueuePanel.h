#pragma once

#include <ftxui/component/component.hpp>

#include <memory>

namespace codeharness::tui
{

struct TuiState;

/// Renders `state->pendingToolCalls` (awaiting ToolScheduler). Empty state
/// collapses to nothing — the panel only appears when there is queued work.
class QueuePanel
{
public:
	static ftxui::Component Create(std::shared_ptr<TuiState> state);
};

} // namespace codeharness::tui
