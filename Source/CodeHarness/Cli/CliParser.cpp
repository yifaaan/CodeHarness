#include "Cli/CliParser.h"

#include <CLI/CLI.hpp>
#include <string>

#include "absl/status/status.h"

namespace codeharness::cli
{

	absl::StatusOr<CliOptions> ParseArgs(int argc, char** argv)
	{
		CliOptions opts;

		CLI::App app{"codeharness — AI coding agent"};
		app.allow_extras(false);

		// -p/--prompt is required for v1 (no interactive stdin mode yet). We
		// capture into a local and validate emptiness ourselves so that --help
		// and --version short-circuit before the "prompt required" check.
		std::string prompt;
		app.add_option("-p,--prompt", prompt, "The prompt to send to the model (one-shot).");
		app.add_option("-m,--model", opts.model, "Model alias; defaults to config's default_model.");
		app.add_option("--workdir", opts.workdir, "Working directory; defaults to the current directory.");
		app.add_flag("-y,--yolo", opts.yolo, "Allow all tool actions without prompting (Yolo permission mode).");
		app.add_flag("-V,--version", opts.version, "Print version and exit.");
		// Note: -h/--help is provided by CLI11's built-in help flag; setting it
		// throws CLI::CallForHelp, which we handle below.

		// CLI11 throws CLI::CallForHelp / CLI::CallForAllHelp on --help/-h and
		// CLI::ParseError on bad usage; --version is just a flag (no throw).
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
			// Surface CLI11's own message (it includes context), then fail.
			std::cout << e.what() << "\n\n" << app.help();
			return absl::InvalidArgumentError(e.what());
		}

		if (opts.version || opts.help)
		{
			return opts;
		}

		if (prompt.empty())
		{
			std::cout << app.help();
			return absl::InvalidArgumentError("--prompt is required (interactive mode is not yet implemented)");
		}
		opts.prompt = std::move(prompt);
		return opts;
	}

} // namespace codeharness::cli
