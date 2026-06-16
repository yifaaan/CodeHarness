#pragma once

#include <memory>

#include <ftxui/component/component.hpp>

namespace codeharness::tui
{

struct TuiState;

/// Kimi-style top banner shown above an active transcript.
class Banner
{
public:
	static ftxui::Component Create(std::shared_ptr<TuiState> state);
};

} // namespace codeharness::tui
