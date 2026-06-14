#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Host.h"

namespace codeharness::host
{

	Host *GetCurrentHost();
	Host *GetCurrentHostNoThrow();
	void SetCurrentHost(Host *host);
	void ResetCurrentHost();

	template <typename F>
	auto RunWithHost(Host &host, F &&fn) -> decltype(fn())
	{
		auto *prev = GetCurrentHostNoThrow();
		SetCurrentHost(&host);
		struct Guard
		{
			Host *prev;
			~Guard()
			{
				if (prev)
					SetCurrentHost(prev);
				else
					ResetCurrentHost();
			}
		} guard{prev};
		return fn();
	}

	absl::StatusOr<std::string> Normpath(std::string_view path);
	std::string PathClass();
	absl::StatusOr<std::string> GetHome();
	absl::StatusOr<std::string> GetCwd();

	absl::Status Chdir(std::string_view path);
	absl::StatusOr<StatResult> Stat(std::string_view path, bool followSymlinks = true);
	absl::StatusOr<std::vector<std::string>> Iterdir(std::string_view path);
	absl::StatusOr<std::vector<std::string>> Glob(std::string_view pattern, std::string_view path = "", const GlobOptions &options = {});

	absl::StatusOr<std::vector<uint8_t>> ReadBytes(std::string_view path);
	absl::StatusOr<std::string> ReadText(std::string_view path);
	absl::StatusOr<std::vector<std::string>> ReadLines(std::string_view path, int count = 0);
	absl::Status WriteBytes(std::string_view path, std::span<const uint8_t> data);
	absl::Status WriteText(std::string_view path, std::string_view data);
	absl::Status Mkdir(std::string_view path, const MkdirOptions &options = {});

	absl::StatusOr<std::unique_ptr<HostProcess>> Exec(std::string_view command, std::string_view cwd = "");
	absl::StatusOr<std::unique_ptr<HostProcess>> ExecWithEnv(
		std::vector<std::string> args,
		std::string_view cwd = "",
		const std::vector<std::pair<std::string, std::string>> &env = {});

} // namespace codeharness::host