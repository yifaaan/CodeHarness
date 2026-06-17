#pragma once

#include <ftxui/component/component.hpp>

#include <memory>

namespace codeharness::tui
{

	struct TuiState;

	/// Bottom-of-transcript banner shown while `state->compacting` is true.
	/// Renders an empty Element when no compaction is in progress.
	class CompactionIndicator
	{
	public:
		static ftxui::Component Create(std::shared_ptr<TuiState> state);
	};

} // namespace codeharness::tui
