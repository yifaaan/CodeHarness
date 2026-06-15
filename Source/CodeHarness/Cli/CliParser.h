#pragma once

#include "Cli/CliOptions.h"
#include "absl/status/statusor.h"

namespace codeharness::cli
{

	// Parse argv into CliOptions. On --help/-h the `help` flag is set and usage
	// has already been printed to stdout; on -V/--version the `version` flag is
	// set. Returns an error status on invalid usage (e.g. missing --prompt).
	//
	// `argv` is taken as (argc, argv) from main; argv[0] (program name) is used
	// only for the CLI11 app label and is not stored.
	absl::StatusOr<CliOptions> ParseArgs(int argc, char** argv);

} // namespace codeharness::cli
