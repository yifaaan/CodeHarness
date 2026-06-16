#pragma once

#include <ftxui/component/component.hpp>

namespace codeharness::tui
{

/// Renders a single message entry (user, assistant, system) in the transcript.
class MessageEntry
{
public:
	static ftxui::Component Create();
};

} // namespace codeharness::tui