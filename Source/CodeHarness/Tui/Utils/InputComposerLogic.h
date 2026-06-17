#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Tui/Utils/SlashCommands.h"

namespace codeharness::tui
{

	struct SlashSuggestion
	{
		SlashCommands::Command command;
		std::string display;
	};

	struct FileMentionQuery
	{
		std::size_t at = 0;
		std::string prefix;
	};

	struct FileMentionSuggestion
	{
		std::string insertText;
		std::string display;
		bool isDirectory = false;
	};

	struct HistoryNavigationState
	{
		std::size_t cursor = 0;
		std::string savedInput;
	};

	enum class ComposerSubmitAction
	{
		Submit,
		InsertNewline,
		None,
	};

	std::vector<SlashSuggestion> BuildSlashSuggestions(std::string_view input, std::size_t limit = 8);
	std::optional<FileMentionQuery> FindFileMentionQuery(std::string_view input, std::size_t cursor);
	std::vector<FileMentionSuggestion> BuildFileMentionSuggestions(std::string_view workdir,
												 std::string_view prefix,
												 std::size_t limit = 8);
	std::string ApplyFileMentionCompletion(std::string_view input,
										 const FileMentionQuery& query,
										 std::string_view insertText);
	bool IsShiftEnterInputSequence(std::string_view input);
	ComposerSubmitAction SubmitAction(bool shiftHeld, bool inputEmpty);
	bool CanUseHistoryForInput(std::string_view input);
	bool ApplyHistoryUp(const std::vector<std::string>& entries, HistoryNavigationState& nav, std::string& input);
	bool ApplyHistoryDown(const std::vector<std::string>& entries, HistoryNavigationState& nav, std::string& input);

} // namespace codeharness::tui
