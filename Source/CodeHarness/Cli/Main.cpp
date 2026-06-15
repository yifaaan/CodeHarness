#include <iostream>
#include <string>

#include "Cli/CliParser.h"
#include "Cli/RunPrompt.h"
#include "Host/LocalHost.h"
#include "Llm/BeastHttpClient.h"
#include "absl/status/status.h"
#include "fmt/format.h"
#include "spdlog/spdlog.h"

namespace
{

	void InitLogging()
	{
		// Warn by default; let SPDLOG_LEVEL or CODEHARNESS_LOG_LEVEL raise it.
		const char* lvl = std::getenv("SPDLOG_LEVEL");
		if (lvl == nullptr || lvl[0] == '\0')
		{
			lvl = std::getenv("CODEHARNESS_LOG_LEVEL");
		}
		std::string level = (lvl && lvl[0]) ? std::string(lvl) : "warn";
		spdlog::set_level(spdlog::level::from_str(level));
	}

} // namespace

int main(int argc, char** argv)
{
	InitLogging();

	auto opts = codeharness::cli::ParseArgs(argc, argv);
	if (!opts.ok())
	{
		fmt::print(stderr, "codeharness: {}\n", opts.status().message());
		return 1;
	}
	if (opts->help)
	{
		// Usage already printed by the parser.
		return 0;
	}
	if (opts->version)
	{
		fmt::print("codeharness 0.1.0\n");
		return 0;
	}

	codeharness::host::LocalHost host;
	codeharness::llm::BeastHttpClient http;

	codeharness::cli::RunDeps deps{
		.host = &host,
		.http = &http,
		.resolveProvider = {}, // production: resolve from config inside Run
	};

	auto status = codeharness::cli::Run(*opts, deps);
	if (!status.ok())
	{
		fmt::print(stderr, "codeharness: {}\n", status.message());
		return 2;
	}
	return 0;
}
