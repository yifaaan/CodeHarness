#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Cli/CliParser.h"
#include "Cli/RunPrompt.h"
#include "Host/LocalHost.h"
#include "Llm/BeastHttpClient.h"
#include "Tui/TuiApp.h"
#include "absl/status/status.h"
#include "fmt/format.h"
#include "spdlog/logger.h"
#include "spdlog/sinks/basic_file_sink.h"
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
		const std::string level = (lvl && lvl[0]) ? std::string(lvl) : "warn";
		const auto parsedLevel = spdlog::level::from_str(level);

		try
		{
			auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log.log", true);
			std::vector<spdlog::sink_ptr> sinks{fileSink};
			auto logger = std::make_shared<spdlog::logger>("codeharness", sinks.begin(), sinks.end());
			logger->set_level(parsedLevel);

			spdlog::set_default_logger(std::move(logger));
			spdlog::set_level(parsedLevel);
			spdlog::flush_on(spdlog::level::trace);
		}
		catch (const spdlog::spdlog_ex& ex)
		{
			fmt::print(stderr, "codeharness: failed to initialize file logging: {}\n", ex.what());
			spdlog::set_level(parsedLevel);
			spdlog::flush_on(spdlog::level::trace);
		}
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

	if (opts->mode == codeharness::cli::CliMode::Tui)
	{
		auto tuiStatus = codeharness::tui::Run(&host, &http, *opts);
		if (!tuiStatus.ok())
		{
			fmt::print(stderr, "codeharness: {}\n", tuiStatus.message());
			return 2;
		}
		return 0;
	}

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
