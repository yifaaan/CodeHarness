#pragma once

#include <string>

namespace codeharness::cli
{

	enum class CliMode
	{
		Prompt,
		Shell,
		Tui,
	};

	enum class OutputFormat
	{
		Text,
		StreamJson,
	};

	// Parsed command-line options for the CLI. Prompt mode is the one-shot path;
	// shell mode keeps one live session for multiple prompts.
	struct CliOptions
	{
		std::string prompt;	   // -p/--prompt (required in prompt mode)
		std::string model;	   // -m/--model; empty means config defaultModel
		std::string workdir;   // --workdir; empty means process cwd
		std::string skill;	   // -s/--skill: activate skill before prompt (format: name[:args])
		std::string sessionId; // --session: resume session id/prefix in shell mode
		CliMode mode = CliMode::Prompt;
		OutputFormat outputFormat = OutputFormat::Text;
		bool continueLast = false; // --continue: resume latest session for workdir
		bool yolo = false;		   // -y/--yolo: allow-all permission mode
		bool help = false;		   // -h/--help
		bool version = false;	   // -V/--version
	};

} // namespace codeharness::cli
