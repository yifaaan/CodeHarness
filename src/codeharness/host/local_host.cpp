#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <fileapi.h>
// clang-format on
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <fmt/format.h>
#include <glob/glob.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <reproc++/reproc.hpp>
#include <sstream>
#include <stop_token>
#include <system_error>

#include "environment.h"
#include "local_host.h"

namespace codeharness::host
{

namespace
{

std::string ReadFileContents(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::ate);
	if (!file.is_open())
		return {};
	auto size = file.tellg();
	file.seekg(0);
	std::string content(static_cast<size_t>(size), '\0');
	file.read(content.data(), static_cast<std::streamsize>(content.size()));
	return content;
}

absl::Status WriteFileContents(const std::filesystem::path& path, std::string_view data)
{
	std::ofstream file(path, std::ios::binary);
	if (!file.is_open())
	{
		return absl::InternalError(fmt::format("failed to write file: {}", path.string()));
	}
	file.write(data.data(), static_cast<std::streamsize>(data.size()));
	return absl::OkStatus();
}

class ReprocProcess : public HostProcess
{
public:
	explicit ReprocProcess(reproc::process _proc) : proc(std::move(_proc)) {}

	absl::Status WriteStdin(std::string_view data) override
	{
		auto [written, ec] = proc.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
		if (ec)
			return absl::InternalError(fmt::format("failed to write stdin: {}", ec.message()));
		return absl::OkStatus();
	}

	absl::Status CloseStdin() override
	{
		auto ec = proc.close(reproc::stream::in);
		if (ec)
			return absl::InternalError(fmt::format("failed to close stdin: {}", ec.message()));
		return absl::OkStatus();
	}

	absl::StatusOr<std::string> ReadStdout() override
	{
		std::string result;
		std::array<uint8_t, 4096> buffer{};
		for (;;)
		{
			auto [bytes, ec] = proc.read(reproc::stream::out, buffer.data(), buffer.size());
			if (ec == std::errc::broken_pipe)
				break;
			if (ec)
				return absl::InternalError(fmt::format("failed to read stdout: {}", ec.message()));
			if (bytes == 0)
				break;
			result.append(reinterpret_cast<char*>(buffer.data()), bytes);
		}
		return result;
	}

	absl::StatusOr<std::string> ReadStderr() override
	{
		std::string result;
		std::array<uint8_t, 4096> buffer{};
		for (;;)
		{
			auto [bytes, ec] = proc.read(reproc::stream::err, buffer.data(), buffer.size());
			if (ec == std::errc::broken_pipe)
				break;
			if (ec)
				return absl::InternalError(fmt::format("failed to read stderr: {}", ec.message()));
			if (bytes == 0)
				break;
			result.append(reinterpret_cast<char*>(buffer.data()), bytes);
		}
		return result;
	}

	absl::StatusOr<int> Pid() const override
	{
		auto& mutableProc = const_cast<reproc::process&>(proc);
		auto [pid, ec] = mutableProc.pid();
		if (ec)
			return absl::InternalError(fmt::format("failed to get pid: {}", ec.message()));
		return pid;
	}

	absl::StatusOr<int> ExitCode() const override
	{
		auto& mutableProc = const_cast<reproc::process&>(proc);
		auto [code, ec] = mutableProc.wait(reproc::milliseconds(0));
		if (ec)
			return -1;
		return code;
	}

	absl::StatusOr<int> Wait() override
	{
		auto [code, ec] = proc.wait(reproc::infinite);
		if (ec)
			return absl::InternalError(fmt::format("wait failed: {}", ec.message()));
		return code;
	}

	absl::Status Kill(const std::string& signal) override
	{
		auto ec = proc.terminate();
		if (ec)
			return absl::OkStatus();
		auto [code, waitEc] = proc.wait(reproc::milliseconds(5000));
		if (waitEc == std::errc::timed_out)
		{
			proc.kill();
		}
		return absl::OkStatus();
	}

	absl::StatusOr<DrainResult> Drain(int timeoutMs, std::stop_token stopToken) override
	{
		DrainResult result;
		constexpr int kSliceMs = 50;
		constexpr std::size_t kMaxPerStream = 1024 * 1024; // 1 MB per stream
		std::array<uint8_t, 4096> buffer{};

		bool outDone = false;
		bool errDone = false;
		bool exited = false;

		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

		auto readStream = [&](reproc::stream s, std::string& dest, bool& done) {
			auto [n, ec] = proc.read(s, buffer.data(), buffer.size());
			if (ec || n == 0)
			{
				done = true; // EOF (broken_pipe / 0 bytes) or error
				return;
			}
			dest.append(reinterpret_cast<char*>(buffer.data()), n);
			if (dest.size() >= kMaxPerStream)
				done = true; // stop runaway output
		};

		while (true)
		{
			if (outDone && errDone && exited)
				return result;
			if (stopToken.stop_requested())
				return result; // Finished stays false
			if (timeoutMs > 0 && std::chrono::steady_clock::now() >= deadline)
			{
				result.timedOut = true;
				return result;
			}

			int interests = 0;
			if (!outDone)
				interests |= reproc::event::out;
			if (!errDone)
				interests |= reproc::event::err;
			if (!exited)
				interests |= reproc::event::exit;

			auto [events, ec] = proc.poll(interests, reproc::milliseconds(kSliceMs));
			if (ec && ec != std::errc::timed_out)
			{
				return absl::InternalError(fmt::format("poll failed: {}", ec.message()));
			}
			if (events & reproc::event::out)
			{
				readStream(reproc::stream::out, result.out, outDone);
			}
			if (events & reproc::event::err)
			{
				readStream(reproc::stream::err, result.err, errDone);
			}
			if (events & reproc::event::exit)
			{
				auto [code, waitEc] = proc.wait(reproc::milliseconds(0));
				if (!waitEc)
				{
					exited = true;
					result.exitCode = code;
					result.finished = true;
				}
			}
		}
	}

private:
	reproc::process proc;
};

} // namespace

LocalHost::LocalHost(std::string_view _cwd)
{
	if (!_cwd.empty())
	{
		cwd = std::filesystem::absolute(std::filesystem::path(_cwd));
	}
	else
	{
		std::error_code ec;
		cwd = std::filesystem::current_path(ec);
		if (ec)
			cwd = ".";
	}

	auto env = DetectEnvironment();
	shellPath = env.shellPath;

	shellName = env.shellName;
	spdlog::debug("LocalHost created: cwd={}, shell={}", cwd.string(), shellPath);
}

std::filesystem::path LocalHost::ResolvePath(std::string_view path) const
{
	auto p = std::filesystem::path(path);
	if (p.is_absolute())
		return p.lexically_normal();
	return (cwd / p).lexically_normal();
}

std::string LocalHost::PathClass() const
{
#ifdef _WIN32
	return "win32";
#else
	return "posix";
#endif
}

absl::StatusOr<std::string> LocalHost::Normpath(std::string_view path) const
{
	return std::filesystem::path(path).lexically_normal().string();
}

absl::StatusOr<std::string> LocalHost::GetHome() const
{
	const char* home = std::getenv("HOME");
	if (!home)
		home = std::getenv("USERPROFILE");
	if (!home)
		return absl::InternalError("cannot determine home directory");
	return std::string(home);
}

absl::StatusOr<std::string> LocalHost::GetCwd() const
{
	return cwd.string();
}

absl::Status LocalHost::Chdir(std::string_view path)
{
	auto p = ResolvePath(path);
	std::error_code ec;
	auto status = std::filesystem::status(p, ec);
	if (ec)
		return absl::NotFoundError(fmt::format("cannot access path: {}", p.string()));
	if (!std::filesystem::is_directory(status))
	{
		return absl::InvalidArgumentError(fmt::format("not a directory: {}", p.string()));
	}
	cwd = p;
	spdlog::debug("Chdir: {}", p.string());
	return absl::OkStatus();
}

absl::StatusOr<StatResult> LocalHost::Stat(std::string_view path, bool followSymlinks)
{
	auto p = ResolvePath(path);
	std::error_code ec;

	std::filesystem::file_status fs;
	if (followSymlinks)
	{
		fs = std::filesystem::status(p, ec);
	}
	else
	{
		fs = std::filesystem::symlink_status(p, ec);
	}

	if (ec)
	{
		if (ec.value() == 2)
		{
			return absl::NotFoundError(fmt::format("file not found: {}", p.string()));
		}
		if (ec == std::errc::permission_denied || ec.value() == 5)
		{
			return absl::PermissionDeniedError(fmt::format("permission denied: {}", p.string()));
		}
		return absl::InternalError(fmt::format("stat failed: {}: {}", p.string(), ec.message()));
	}

	if (!std::filesystem::exists(fs))
	{
		return absl::NotFoundError(fmt::format("file not found: {}", p.string()));
	}

	StatResult result{};

	if (std::filesystem::is_regular_file(fs))
	{
		result.stMode |= 0100000;
	}
	else if (std::filesystem::is_directory(fs))
	{
		result.stMode |= 0040000;
	}
	else if (std::filesystem::is_symlink(fs))
	{
		result.stMode |= 0120000;
	}

	if (std::filesystem::is_regular_file(fs))
	{
		result.stSize = static_cast<int64_t>(std::filesystem::file_size(p, ec));
	}

#ifdef _WIN32
	HANDLE hFile = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		BY_HANDLE_FILE_INFORMATION info;
		if (GetFileInformationByHandle(hFile, &info))
		{
			result.stDev = info.dwVolumeSerialNumber;
			result.stIno = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
			result.stNlink = info.nNumberOfLinks;
			result.stSize = (static_cast<int64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
			auto toUnix = [](const FILETIME& ft) -> int64_t {
				ULARGE_INTEGER li;
				li.LowPart = ft.dwLowDateTime;
				li.HighPart = ft.dwHighDateTime;
				constexpr int64_t EPOCH_DIFF = 11644473600LL;
				return static_cast<int64_t>(li.QuadPart / 10000000) - EPOCH_DIFF;
			};
			result.stAtime = toUnix(info.ftLastAccessTime);
			result.stMtime = toUnix(info.ftLastWriteTime);
			result.stCtime = toUnix(info.ftCreationTime);
		}
		CloseHandle(hFile);
	}
#else
	struct stat nativeStat;
	auto assignFromNative = [&](const struct stat& st) {
		result.stDev = st.st_dev;
		result.stIno = st.st_ino;
		result.stNlink = st.st_nlink;
		result.stUid = st.st_uid;
		result.stGid = st.st_gid;
		result.stSize = st.st_size;
		result.stAtime = st.st_atime;
		result.stMtime = st.st_mtime;
		result.stCtime = st.st_ctime;
		result.stMode = st.st_mode;
	};
	if (followSymlinks)
	{
		if (::stat(p.c_str(), &nativeStat) == 0)
		{
			assignFromNative(nativeStat);
		}
	}
	else
	{
		if (::lstat(p.c_str(), &nativeStat) == 0)
		{
			assignFromNative(nativeStat);
		}
	}
#endif

	return result;
}

absl::StatusOr<std::vector<std::string>> LocalHost::Iterdir(std::string_view path)
{
	auto p = ResolvePath(path);
	std::error_code ec;
	std::vector<std::string> entries;

	for (auto it = std::filesystem::directory_iterator(p, ec); it != std::filesystem::directory_iterator(); ++it)
	{
		entries.push_back(it->path().filename().string());
	}

	if (ec)
	{
		return absl::InternalError(fmt::format("failed to list directory: {}: {}", p.string(), ec.message()));
	}

	std::sort(entries.begin(), entries.end());
	return entries;
}

absl::StatusOr<std::vector<std::string>> LocalHost::Glob(std::string_view pattern, std::string_view path, const GlobOptions& options)
{
	auto root = path.empty() ? cwd : ResolvePath(path);
	auto fullPattern = (root / pattern).string();

	spdlog::debug("Glob: pattern={}, root={}", fullPattern, root.string());

	std::vector<std::filesystem::path> matches;
	try
	{
		bool recursive = std::string(pattern).find("**") != std::string::npos;
		if (recursive)
		{
			matches = glob::rglob(fullPattern);
		}
		else
		{
			matches = glob::glob(fullPattern);
		}
	}
	catch (const std::exception& e)
	{
		return absl::InternalError(fmt::format("glob failed: {}", e.what()));
	}

	std::vector<std::string> results;
	for (const auto& m : matches)
	{
		auto normal = std::filesystem::absolute(m).lexically_normal().string();
		if (!options.includeDirs)
		{
			std::error_code ec;
			if (std::filesystem::is_directory(m, ec))
				continue;
		}
		results.push_back(normal);
	}

	std::sort(results.begin(), results.end());
	return results;
}

absl::StatusOr<std::vector<uint8_t>> LocalHost::ReadBytes(std::string_view path)
{
	auto p = ResolvePath(path);
	std::ifstream file(p, std::ios::binary | std::ios::ate);
	if (!file.is_open())
	{
		return absl::NotFoundError(fmt::format("file not found: {}", p.string()));
	}
	auto size = file.tellg();
	file.seekg(0);
	std::vector<uint8_t> content(static_cast<size_t>(size));
	file.read(reinterpret_cast<char*>(content.data()), static_cast<std::streamsize>(content.size()));
	return content;
}

absl::StatusOr<std::string> LocalHost::ReadText(std::string_view path)
{
	auto p = ResolvePath(path);
	auto content = ReadFileContents(p);
	if (content.empty())
	{
		std::error_code ec;
		if (!std::filesystem::exists(p, ec))
		{
			return absl::NotFoundError(fmt::format("file not found: {}", p.string()));
		}
	}
	return content;
}

absl::StatusOr<std::vector<std::string>> LocalHost::ReadLines(std::string_view path, int count)
{
	auto p = ResolvePath(path);
	std::ifstream file(p);
	if (!file.is_open())
	{
		return absl::NotFoundError(fmt::format("file not found: {}", p.string()));
	}

	std::vector<std::string> lines;
	std::string line;
	while (std::getline(file, line))
	{
		lines.push_back(line);
		if (count > 0 && static_cast<int>(lines.size()) >= count)
			break;
	}
	return lines;
}

absl::Status LocalHost::WriteBytes(std::string_view path, std::span<const uint8_t> data)
{
	auto p = ResolvePath(path);
	return WriteFileContents(p, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

absl::Status LocalHost::WriteText(std::string_view path, std::string_view data)
{
	return WriteFileContents(ResolvePath(path), data);
}

absl::Status LocalHost::Mkdir(std::string_view path, const MkdirOptions& options)
{
	auto p = ResolvePath(path);
	std::error_code ec;
	bool created = false;

	if (options.recursive)
	{
		created = std::filesystem::create_directories(p, ec);
	}
	else
	{
		created = std::filesystem::create_directory(p, ec);
	}

	if (!created && !ec)
	{
		if (!options.existOk)
		{
			return absl::AlreadyExistsError(fmt::format("directory already exists: {}", p.string()));
		}
	}
	else if (ec)
	{
		return absl::InternalError(fmt::format("failed to create directory: {}: {}", p.string(), ec.message()));
	}

	return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<HostProcess>> LocalHost::Exec(std::string_view command, std::string_view cwd)
{
	std::string workDirStr;
	if (!cwd.empty())
	{
		workDirStr = std::filesystem::absolute(std::filesystem::path(cwd)).string();
	}

	reproc::process proc;
	reproc::options opts;
	opts.redirect.out.type = reproc::redirect::pipe;
	opts.redirect.err.type = reproc::redirect::pipe;
	opts.redirect.in.type = reproc::redirect::pipe;

	if (!workDirStr.empty())
	{
		opts.working_directory = workDirStr.c_str();
	}

	std::string shell = shellPath;
#ifdef _WIN32
	auto hasNonAscii = [](const std::string& s) {
		return std::any_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) > 127; });
	};
	if (hasNonAscii(shell))
	{
		shell = "cmd.exe";
	}
#endif

	std::vector<std::string> args;
	if (shellName == "cmd" || shell == "cmd.exe")
	{
		args = {shell, "/c", std::string(command)};
	}
	else
	{
		args = {shell, "-c", std::string(command)};
	}

	spdlog::debug("Exec: shell={}, cmd={}", shell, command);

	auto ec = proc.start(args, opts);
	if (ec)
	{
		return absl::InternalError(fmt::format("failed to spawn process: {}", ec.message()));
	}

	return std::make_unique<ReprocProcess>(std::move(proc));
}

absl::StatusOr<std::unique_ptr<HostProcess>> LocalHost::ExecWithEnv(
	std::vector<std::string> args,
	std::string_view cwd,
	const std::vector<std::pair<std::string, std::string>>& env)
{
	std::string workDirStr;
	if (!cwd.empty())
	{
		workDirStr = std::filesystem::absolute(std::filesystem::path(cwd)).string();
	}

	reproc::process proc;
	reproc::options opts;
	opts.redirect.out.type = reproc::redirect::pipe;
	opts.redirect.err.type = reproc::redirect::pipe;
	opts.redirect.in.type = reproc::redirect::pipe;

	if (!workDirStr.empty())
	{
		opts.working_directory = workDirStr.c_str();
	}

	if (!env.empty())
	{
		opts.env.extra = reproc::env(env);
	}

	auto ec = proc.start(args, opts);
	if (ec)
	{
		return absl::InternalError(fmt::format("failed to spawn process: {}", ec.message()));
	}

	return std::make_unique<ReprocProcess>(std::move(proc));
}

} // namespace codeharness::host