#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace codeharness::host
{

	// Result of draining a process's output streams until exit/timeout/cancel.
	struct DrainResult
	{
		std::string out;
		std::string err;
		int exitCode = -1;
		bool finished = false;
		bool timedOut = false;
	};

	class HostProcess
	{
	public:
		virtual ~HostProcess() = default;

		virtual absl::Status WriteStdin(std::string_view data) = 0;
		virtual absl::Status CloseStdin() = 0;
		virtual absl::StatusOr<std::string> ReadStdout() = 0;
		virtual absl::StatusOr<std::string> ReadStderr() = 0;
		virtual absl::StatusOr<int> Pid() const = 0;
		virtual absl::StatusOr<int> ExitCode() const = 0;
		virtual absl::StatusOr<int> Wait() = 0;
		virtual absl::Status Kill(const std::string& signal = "SIGTERM") = 0;

		// Drain stdout and stderr concurrently (avoiding pipe-buffer deadlock) while
		// waiting for the process to exit. Returns when the process exits, `TimeoutMs`
		// elapses (<= 0 disables the timeout), or `StopToken` is requested. Does NOT
		// kill the process; the caller should kill if `Finished` is false.
		//
		// Disambiguates the non-finished case via `TimedOut`: if `Finished` is false
		// and `TimedOut` is false, the drain was cancelled via `StopToken`.
		virtual absl::StatusOr<DrainResult> Drain(int timeoutMs, std::stop_token stopToken) = 0;
	};

} // namespace codeharness::host