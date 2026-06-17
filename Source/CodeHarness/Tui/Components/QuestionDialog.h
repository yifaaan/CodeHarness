#pragma once

#include <functional>
#include <optional>
#include <string>

#include <ftxui/component/component.hpp>

#include "Tools/AskUser.h"

namespace codeharness::tui
{

	struct QuestionDialogOptions
	{
		std::function<std::optional<tools::QuestionRequest>()> request;
		std::function<void(std::string)> onAnswer;
		std::function<void()> onToggleToolOutput;
	};

	class QuestionDialog
	{
	public:
		static ftxui::Component Create(QuestionDialogOptions options);
	};

} // namespace codeharness::tui
