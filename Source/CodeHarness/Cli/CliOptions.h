#pragma once

#include <string>

namespace codeharness::cli
{

	// Parsed command-line options for the non-interactive CLI. MVP scope: a
	// single `--prompt` run against a configured model. The TUI/shell mode,
	// `--continue`/`--session`, and `--output-format stream-json` are deferred.
	struct CliOptions
	{
		std::string prompt; // -p/--prompt (required for v1)
		std::string model;	// -m/--model; empty → config defaultModel
		std::string workdir; // --workdir; empty → process cwd
		bool yolo = false;	// -y/--yolo: allow-all permission mode
		bool help = false;	// -h/--help
		bool version = false; // -V/--version
	};

} // namespace codeharness::cli
