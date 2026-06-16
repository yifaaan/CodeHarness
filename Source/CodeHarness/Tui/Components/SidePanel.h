#pragma once

#include <ftxui/component/component.hpp>

#include <memory>

namespace codeharness::tui
{

struct TuiState;

/// Persistent sidebar (Ctrl+B to toggle) showing session/model/usage info.
/// Renders an empty Element when `state->sidePanelVisible` is false.
class SidePanel
{
public:
	static ftxui::Component Create(std::shared_ptr<TuiState> state);
};

} // namespace codeharness::tui
