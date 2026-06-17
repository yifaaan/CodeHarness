#include "Tui/Components/MessageEntry.h"

#include <string>
#include <string_view>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "Tui/Renderers/MarkdownRenderer.h"

namespace codeharness::tui
{

	namespace
	{

		constexpr std::string_view kUserBullet = "✨ ";
		constexpr std::string_view kStatusBullet = "● ";
		constexpr std::string_view kMessageIndent = "  ";
		constexpr std::string_view kFailureMark = "✗ ";

	} // namespace

	ftxui::Element MessageEntry::RenderUser(std::string_view textValue)
	{
		using namespace ftxui;
		return vbox({
			text(""),
			hbox({
				text(std::string(kUserBullet)) | bold | color(Color::Green),
				text(std::string(textValue)) | bold | color(Color::Green),
			}),
		});
	}

	ftxui::Element MessageEntry::RenderAssistant(std::string_view markdown, bool showBullet)
	{
		using namespace ftxui;
		std::string content(markdown);
		if (content.empty())
		{
			return text("");
		}

		const auto prefix = showBullet ? kStatusBullet : kMessageIndent;
		return vbox({
			text(""),
			hbox({
				text(std::string(prefix)) | color(Color::GrayLight),
				MarkdownRenderer::Render(content) | flex,
			}),
		});
	}

	ftxui::Element MessageEntry::RenderSystem(std::string_view textValue)
	{
		using namespace ftxui;
		return hbox({
			text(std::string(kMessageIndent)),
			text(std::string(textValue)) | dim | color(Color::GrayLight),
		});
	}

	ftxui::Element MessageEntry::RenderError(std::string_view textValue)
	{
		using namespace ftxui;
		return hbox({
			text(std::string(kFailureMark)) | bold | color(Color::Red),
			text(std::string(textValue)) | color(Color::Red),
		});
	}

	bool MessageEntry::IsErrorSystemMessage(std::string_view textValue)
	{
		return textValue.rfind("Error:", 0) == 0 ||
			   textValue.find("denied") != std::string_view::npos ||
			   textValue.find("failed") != std::string_view::npos;
	}

} // namespace codeharness::tui
