#include "current_host.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <memory>
#include <mutex>

#include "local_host.h"

namespace codeharness::host
{

namespace
{

thread_local Host* _tlsCurrentHost = nullptr;

struct DefaultHostHolder
{
	std::unique_ptr<Host> instance;
	std::once_flag flag;
};

DefaultHostHolder& DefaultHost()
{
	static DefaultHostHolder holder;
	return holder;
}

void EnsureDefaultHost()
{
	std::call_once(DefaultHost().flag, []() { DefaultHost().instance = std::make_unique<LocalHost>(); });
}

} // namespace

Host* GetCurrentHost()
{
	if (_tlsCurrentHost)
		return _tlsCurrentHost;
	EnsureDefaultHost();
	return DefaultHost().instance.get();
}

Host* GetCurrentHostNoThrow()
{
	return _tlsCurrentHost;
}

void SetCurrentHost(Host* host)
{
	_tlsCurrentHost = host;
}

void ResetCurrentHost()
{
	_tlsCurrentHost = nullptr;
}

absl::StatusOr<std::string> Normpath(std::string_view path)
{
	return GetCurrentHost()->Normpath(path);
}

std::string PathClass()
{
	return GetCurrentHost()->PathClass();
}

absl::StatusOr<std::string> GetHome()
{
	return GetCurrentHost()->GetHome();
}

absl::StatusOr<std::string> GetCwd()
{
	return GetCurrentHost()->GetCwd();
}

absl::Status Chdir(std::string_view path)
{
	return GetCurrentHost()->Chdir(path);
}

absl::StatusOr<StatResult> Stat(std::string_view path, bool followSymlinks)
{
	return GetCurrentHost()->Stat(path, followSymlinks);
}

absl::StatusOr<std::vector<std::string>> Iterdir(std::string_view path)
{
	return GetCurrentHost()->Iterdir(path);
}

absl::StatusOr<std::vector<std::string>> Glob(std::string_view pattern, std::string_view path, const GlobOptions& options)
{
	return GetCurrentHost()->Glob(pattern, path, options);
}

absl::StatusOr<std::vector<uint8_t>> ReadBytes(std::string_view path)
{
	return GetCurrentHost()->ReadBytes(path);
}

absl::StatusOr<std::string> ReadText(std::string_view path)
{
	return GetCurrentHost()->ReadText(path);
}

absl::StatusOr<std::vector<std::string>> ReadLines(std::string_view path, int count)
{
	return GetCurrentHost()->ReadLines(path, count);
}

absl::Status WriteBytes(std::string_view path, std::span<const uint8_t> data)
{
	return GetCurrentHost()->WriteBytes(path, data);
}

absl::Status WriteText(std::string_view path, std::string_view data)
{
	return GetCurrentHost()->WriteText(path, data);
}

absl::Status Mkdir(std::string_view path, const MkdirOptions& options)
{
	return GetCurrentHost()->Mkdir(path, options);
}

absl::StatusOr<std::unique_ptr<HostProcess>> Exec(std::string_view command, std::string_view cwd)
{
	return GetCurrentHost()->Exec(command, cwd);
}

absl::StatusOr<std::unique_ptr<HostProcess>> ExecWithEnv(std::vector<std::string> args, std::string_view cwd, const std::vector<std::pair<std::string, std::string>>& env)
{
	return GetCurrentHost()->ExecWithEnv(std::move(args), cwd, env);
}

} // namespace codeharness::host