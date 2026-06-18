#pragma once

#include <functional>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>

namespace codeharness::tui
{

	struct ChoicePickerRow
	{
		std::string id;
		std::string label;
		std::string description;
		bool current = false;
		bool disabled = false;
	};

	struct ChoicePickerOptions
	{
		std::string title;
		std::string subtitle;
		std::vector<ChoicePickerRow> rows;
		std::function<std::vector<ChoicePickerRow>()> rowSource;
		std::function<void(ChoicePickerRow)> onSelect;
		std::function<void()> onCancel;
	};

	class ChoicePicker
	{
	public:
		static ftxui::Component Create(ChoicePickerOptions options);
	};

} // namespace codeharness::tui
