#include "Tools/Bash.h"

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fmt/format.h"
#include "Host/Host.h"
#include "Host/HostProcess.h"
#include "Tools/ToolOutput.h"

namespace codeharness::tools
{

	namespace
	{

		constexpr int kDefaultTimeoutMs = 60000;
		constexpr int kMaxTimeoutMs = 300000;

		int ClampTimeout(int requested)
		{
			if (requested <= 0)
				return kDefaultTimeoutMs;
			if (requested > kMaxTimeoutMs)
				return kMaxTimeoutMs;
			return requested;
		}

		std::string GetCommand(const nlohmann::json &args)
		{
			return args.value("command", std::string{});
		}

	} // namespace

	std::string BashTool::Description() const
	{
		return "Execute a shell command and return its stdout, stderr, and exit code. Runs in the "
			   "foreground with a timeout (default 60s, max 300s); on timeout or cancellation the "
			   "process is terminated. Stdin is closed, so interactive commands receive EOF.";
	}

	nlohmann::json BashTool::Parameters() const
	{
		return {
			{"type", "object"},
			{"properties",
			 {{"command", {{"type", "string"}, {"description", "The shell command to execute."}}},
			  {"cwd", {{"type", "string"}, {"description", "Working directory for the command. Defaults to the host cwd."}}},
			  {"timeout_ms",
			   {{"type", "integer"},
				{"default", kDefaultTimeoutMs},
				{"description", "Timeout in milliseconds (max 300000)."}}}}},
			{"required", nlohmann::json::array({"command"})},
		};
	}

	absl::StatusOr<engine::ToolExecution> BashTool::ResolveExecution(const nlohmann::json &args)
	{
		auto command = GetCommand(args);
		if (command.empty())
			return absl::InvalidArgumentError("'command' is required");
		return engine::ToolExecution{.description = fmt::format("Bash: {}", command), .requiresPermission = true};
	}

	absl::StatusOr<engine::ToolResult> BashTool::Execute(const nlohmann::json &args, const engine::ToolContext &ctx)
	{
		if (!ctx.host)
			return absl::FailedPreconditionError("no host available");
		auto command = GetCommand(args);
		if (command.empty())
			return absl::InvalidArgumentError("'command' is required");

		std::string cwd = args.value("cwd", std::string{});
		int timeoutMs = ClampTimeout(args.value("timeout_ms", kDefaultTimeoutMs));

		auto proc = ctx.host->Exec(command, cwd);
		if (!proc.ok())
			return std::move(proc).status();

		// Close stdin so interactive commands receive EOF immediately.
		(void)(*proc)->CloseStdin();

		// Drain output without pipe-buffer deadlock, honouring the timeout and
		// cancellation token. All process interaction happens on this single thread
		// (reproc is not thread-safe).
		auto drain = (*proc)->Drain(timeoutMs, ctx.stopToken);
		if (!drain.ok())
			return std::move(drain).status();

		bool cancelled = false;
		if (!drain->finished)
		{
			// Not finished and not timed out => cancelled via stopToken.
			cancelled = !drain->timedOut;
			(void)(*proc)->Kill("SIGTERM");
			if (auto wait = (*proc)->Wait(); wait.ok())
				drain->exitCode = *wait;
		}

		std::string report;
		if (!drain->out.empty())
			report += drain->out;
		if (!drain->err.empty())
		{
			if (!report.empty() && report.back() != '\n')
				report += '\n';
			report += drain->err;
		}
		if (report.empty() || report.back() != '\n')
			report += '\n';

		if (cancelled)
		{
			report += "[command cancelled]\n";
		}
		else if (drain->timedOut)
		{
			report += fmt::format("[command timed out after {}ms and was killed]\n", timeoutMs);
		}
		report += fmt::format("[exit code: {}]\n", drain->exitCode);

		bool isError = cancelled || drain->timedOut || drain->exitCode != 0;
		report = TruncateOutput(report);
		return engine::ToolResult{.content = std::move(report), .isError = isError};
	}

} // namespace codeharness::tools
