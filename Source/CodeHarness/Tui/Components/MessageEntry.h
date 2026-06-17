#pragma once

#include <string_view>

#include <ftxui/dom/elements.hpp>

namespace codeharness::tui
{

	/// Renders a single message entry (user, assistant, system) in the transcript.
	class MessageEntry
	{
	public:
		static ftxui::Element RenderUser(std::string_view text);
		static ftxui::Element RenderAssistant(std::string_view markdown, bool showBullet = true);
		static ftxui::Element RenderSystem(std::string_view text);
		static ftxui::Element RenderError(std::string_view text);
		static bool IsErrorSystemMessage(std::string_view text);
	};

} // namespace codeharness::tui
