#pragma once

#include <ftxui/component/component.hpp>

#include <memory>

namespace codeharness::tui
{

struct TuiState;

/// Single-line strip below the transcript showing the current activity:
/// "Thinking…", "Reading file.txt", "Running: ls -la". Empty when idle.
class ActivityIndicator
{
public:
	static ftxui::Component Create(std::shared_ptr<TuiState> state);
};

} // namespace codeharness::tui
