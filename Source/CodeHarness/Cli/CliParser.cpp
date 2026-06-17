#include "Cli/CliParser.h"

#include <CLI/CLI.hpp>
#include <iostream>
#include <string>

#include "absl/status/status.h"

namespace codeharness::cli
{

	absl::StatusOr<CliOptions> ParseArgs(int argc, char** argv)
	{
		CliOptions opts;

		CLI::App app{"codeharness - AI coding agent"};
		app.allow_extras(false);
		app.require_subcommand(0, 1);

		std::string prompt;
		std::string outputFormat = "text";
		app.add_option("-p,--prompt", prompt, "The prompt to send to the model (one-shot).");
		app.add_option("-m,--model", opts.model, "Model alias; defaults to config's default_model.");
		app.add_option("--workdir", opts.workdir, "Working directory; defaults to the current directory.");
		app.add_option("-s,--skill", opts.skill, "Activate a skill before prompting (format: name[:args]).");
		app.add_option("--output-format", outputFormat, "Output format: text or stream-json.")
			->check(CLI::IsMember({"text", "stream-json"}));
		app.add_flag("-y,--yolo", opts.yolo, "Allow all tool actions without prompting (Yolo permission mode).");
		app.add_flag("-V,--version", opts.version, "Print version and exit.");

		auto* shell = app.add_subcommand("shell", "Start an interactive multi-turn shell.");
		shell->add_option("--session", opts.sessionId, "Resume a session by id or unique prefix.");
		shell->add_flag("--continue", opts.continueLast, "Resume the latest session for the current workdir.");

		auto* tui = app.add_subcommand("tui", "Start the full-screen TUI.");
		tui->add_option("--session", opts.sessionId, "Resume a session by id or unique prefix.");
		tui->add_flag("--continue", opts.continueLast, "Resume the latest session for the current workdir.");
		tui->add_option("-m,--model", opts.model, "Model alias; defaults to config's default_model.");
		tui->add_option("--workdir", opts.workdir, "Working directory; defaults to the current directory.");
		tui->add_flag("-y,--yolo", opts.yolo, "Allow all tool actions without prompting (Yolo permission mode).");

		try
		{
			app.parse(argc, argv);
		}
		catch (const CLI::CallForHelp&)
		{
			opts.help = true;
			std::cout << app.help();
			return opts;
		}
		catch (const CLI::ParseError& e)
		{
			std::cout << e.what() << "\n\n"
					  << app.help();
			return absl::InvalidArgumentError(e.what());
		}

		if (opts.version || opts.help)
		{
			return opts;
		}

		opts.outputFormat = outputFormat == "stream-json" ? OutputFormat::StreamJson : OutputFormat::Text;

		if (shell->parsed())
		{
			opts.mode = CliMode::Shell;
			if (!prompt.empty())
			{
				std::cout << app.help();
				return absl::InvalidArgumentError("--prompt cannot be used with shell mode");
			}
			if (!opts.sessionId.empty() && opts.continueLast)
			{
				std::cout << app.help();
				return absl::InvalidArgumentError("--session and --continue are mutually exclusive");
			}
			return opts;
		}

		if (tui->parsed())
		{
			opts.mode = CliMode::Tui;
			if (!prompt.empty())
			{
				std::cout << app.help();
				return absl::InvalidArgumentError("--prompt cannot be used with tui mode");
			}
			if (!opts.sessionId.empty() && opts.continueLast)
			{
				std::cout << app.help();
				return absl::InvalidArgumentError("--session and --continue are mutually exclusive");
			}
			return opts;
		}

		opts.mode = CliMode::Prompt;
		if (!opts.sessionId.empty() || opts.continueLast)
		{
			std::cout << app.help();
			return absl::InvalidArgumentError("--session/--continue are only valid in shell mode");
		}
		if (prompt.empty())
		{
			std::cout << app.help();
			return absl::InvalidArgumentError("--prompt is required (use `codeharness shell` for interactive mode)");
		}
		opts.prompt = std::move(prompt);
		return opts;
	}

} // namespace codeharness::cli
