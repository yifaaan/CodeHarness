#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fileapi.h>
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
#include <cstdlib>
#include <fstream>
#include <reproc++/reproc.hpp>
#include <sstream>
#include <system_error>

#include "environment.h"
#include "local_host.h"

namespace codeharness::host {

namespace {

std::string ReadFileContents(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::ate);
  if (!file.is_open()) return {};
  auto size = file.tellg();
  file.seekg(0);
  std::string content(static_cast<size_t>(size), '\0');
  file.read(content.data(), static_cast<std::streamsize>(content.size()));
  return content;
}

absl::Status WriteFileContents(const std::filesystem::path& path, std::string_view data) {
  std::ofstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return absl::InternalError(fmt::format("failed to write file: {}", path.string()));
  }
  file.write(data.data(), static_cast<std::streamsize>(data.size()));
  return absl::OkStatus();
}

class ReprocProcess : public HostProcess {
 public:
  explicit ReprocProcess(reproc::process proc) : proc_(std::move(proc)) {}

  absl::Status WriteStdin(std::string_view data) override {
    auto [written, ec] = proc_.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    if (ec) return absl::InternalError(fmt::format("failed to write stdin: {}", ec.message()));
    return absl::OkStatus();
  }

  absl::Status CloseStdin() override {
    auto ec = proc_.close(reproc::stream::in);
    if (ec) return absl::InternalError(fmt::format("failed to close stdin: {}", ec.message()));
    return absl::OkStatus();
  }

  absl::StatusOr<std::string> ReadStdout() override {
    std::string result;
    std::array<uint8_t, 4096> buffer{};
    for (;;) {
      auto [bytes, ec] = proc_.read(reproc::stream::out, buffer.data(), buffer.size());
      if (ec == std::errc::broken_pipe) break;
      if (ec) return absl::InternalError(fmt::format("failed to read stdout: {}", ec.message()));
      if (bytes == 0) break;
      result.append(reinterpret_cast<char*>(buffer.data()), bytes);
    }
    return result;
  }

  absl::StatusOr<std::string> ReadStderr() override {
    std::string result;
    std::array<uint8_t, 4096> buffer{};
    for (;;) {
      auto [bytes, ec] = proc_.read(reproc::stream::err, buffer.data(), buffer.size());
      if (ec == std::errc::broken_pipe) break;
      if (ec) return absl::InternalError(fmt::format("failed to read stderr: {}", ec.message()));
      if (bytes == 0) break;
      result.append(reinterpret_cast<char*>(buffer.data()), bytes);
    }
    return result;
  }

  absl::StatusOr<int> Pid() const override {
    auto& mutable_proc = const_cast<reproc::process&>(proc_);
    auto [pid, ec] = mutable_proc.pid();
    if (ec) return absl::InternalError(fmt::format("failed to get pid: {}", ec.message()));
    return pid;
  }

  absl::StatusOr<int> ExitCode() const override {
    auto& mutable_proc = const_cast<reproc::process&>(proc_);
    auto [code, ec] = mutable_proc.wait(reproc::milliseconds(0));
    if (ec) return -1;
    return code;
  }

  absl::StatusOr<int> Wait() override {
    auto [code, ec] = proc_.wait(reproc::infinite);
    if (ec) return absl::InternalError(fmt::format("wait failed: {}", ec.message()));
    return code;
  }

  absl::Status Kill(const std::string& signal) override {
    auto ec = proc_.terminate();
    if (ec) return absl::OkStatus();
    auto [code, wait_ec] = proc_.wait(reproc::milliseconds(5000));
    if (wait_ec == std::errc::timed_out) {
      proc_.kill();
    }
    return absl::OkStatus();
  }

 private:
  reproc::process proc_;
};

}  // namespace

LocalHost::LocalHost(std::string_view cwd) {
  if (!cwd.empty()) {
    cwd_ = std::filesystem::absolute(std::filesystem::path(cwd));
  } else {
    std::error_code ec;
    cwd_ = std::filesystem::current_path(ec);
    if (ec) cwd_ = ".";
  }

  auto env = DetectEnvironment();
  shell_path_ = env.shell_path;
  shell_name_ = env.shell_name;

  spdlog::debug("LocalHost created: cwd={}, shell={}", cwd_.string(), shell_path_);
}

std::filesystem::path LocalHost::ResolvePath(std::string_view path) const {
  auto p = std::filesystem::path(path);
  if (p.is_absolute()) return p.lexically_normal();
  return (cwd_ / p).lexically_normal();
}

std::string LocalHost::PathClass() const {
#ifdef _WIN32
  return "win32";
#else
  return "posix";
#endif
}

absl::StatusOr<std::string> LocalHost::Normpath(std::string_view path) const {
  return std::filesystem::path(path).lexically_normal().string();
}

absl::StatusOr<std::string> LocalHost::GetHome() const {
  const char* home = std::getenv("HOME");
  if (!home) home = std::getenv("USERPROFILE");
  if (!home) return absl::InternalError("cannot determine home directory");
  return std::string(home);
}

absl::StatusOr<std::string> LocalHost::GetCwd() const { return cwd_.string(); }

absl::Status LocalHost::Chdir(std::string_view path) {
  auto p = ResolvePath(path);
  std::error_code ec;
  auto status = std::filesystem::status(p, ec);
  if (ec) return absl::NotFoundError(fmt::format("cannot access path: {}", p.string()));
  if (!std::filesystem::is_directory(status)) {
    return absl::InvalidArgumentError(fmt::format("not a directory: {}", p.string()));
  }
  cwd_ = p;
  spdlog::debug("Chdir: {}", p.string());
  return absl::OkStatus();
}

absl::StatusOr<StatResult> LocalHost::Stat(std::string_view path, bool follow_symlinks) {
  auto p = ResolvePath(path);
  std::error_code ec;

  std::filesystem::file_status fs;
  if (follow_symlinks) {
    fs = std::filesystem::status(p, ec);
  } else {
    fs = std::filesystem::symlink_status(p, ec);
  }

  if (ec) {
    if (ec.value() == 2) {
      return absl::NotFoundError(fmt::format("file not found: {}", p.string()));
    }
    if (ec == std::errc::permission_denied || ec.value() == 5) {
      return absl::PermissionDeniedError(fmt::format("permission denied: {}", p.string()));
    }
    return absl::InternalError(fmt::format("stat failed: {}: {}", p.string(), ec.message()));
  }

  if (!std::filesystem::exists(fs)) {
    return absl::NotFoundError(fmt::format("file not found: {}", p.string()));
  }

  StatResult result{};

  if (std::filesystem::is_regular_file(fs)) {
    result.st_mode |= 0100000;
  } else if (std::filesystem::is_directory(fs)) {
    result.st_mode |= 0040000;
  } else if (std::filesystem::is_symlink(fs)) {
    result.st_mode |= 0120000;
  }

  if (std::filesystem::is_regular_file(fs)) {
    result.st_size = static_cast<int64_t>(std::filesystem::file_size(p, ec));
  }

#ifdef _WIN32
  HANDLE hFile = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (hFile != INVALID_HANDLE_VALUE) {
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileInformationByHandle(hFile, &info)) {
      result.st_dev = info.dwVolumeSerialNumber;
      result.st_ino = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
      result.st_nlink = info.nNumberOfLinks;
      result.st_size = (static_cast<int64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
      auto to_unix = [](const FILETIME& ft) -> int64_t {
        ULARGE_INTEGER li;
        li.LowPart = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;
        constexpr int64_t EPOCH_DIFF = 11644473600LL;
        return static_cast<int64_t>(li.QuadPart / 10000000) - EPOCH_DIFF;
      };
      result.st_atime = to_unix(info.ftLastAccessTime);
      result.st_mtime = to_unix(info.ftLastWriteTime);
      result.st_ctime = to_unix(info.ftCreationTime);
    }
    CloseHandle(hFile);
  }
#else
  struct stat native_stat;
  auto assign_from_native = [&](const struct stat& st) {
    result.st_dev = st.st_dev;
    result.st_ino = st.st_ino;
    result.st_nlink = st.st_nlink;
    result.st_uid = st.st_uid;
    result.st_gid = st.st_gid;
    result.st_size = st.st_size;
    result.st_atime = st.st_atime;
    result.st_mtime = st.st_mtime;
    result.st_ctime = st.st_ctime;
    result.st_mode = st.st_mode;
  };
  if (follow_symlinks) {
    if (::stat(p.c_str(), &native_stat) == 0) {
      assign_from_native(native_stat);
    }
  } else {
    if (::lstat(p.c_str(), &native_stat) == 0) {
      assign_from_native(native_stat);
    }
  }
#endif

  return result;
}

absl::StatusOr<std::vector<std::string>> LocalHost::Iterdir(std::string_view path) {
  auto p = ResolvePath(path);
  std::error_code ec;
  std::vector<std::string> entries;

  for (auto it = std::filesystem::directory_iterator(p, ec); it != std::filesystem::directory_iterator(); ++it) {
    entries.push_back(it->path().filename().string());
  }

  if (ec) {
    return absl::InternalError(fmt::format("failed to list directory: {}: {}", p.string(), ec.message()));
  }

  std::sort(entries.begin(), entries.end());
  return entries;
}

absl::StatusOr<std::vector<std::string>> LocalHost::Glob(std::string_view pattern, std::string_view path,
                                                         const GlobOptions& options) {
  auto root = path.empty() ? cwd_ : ResolvePath(path);
  auto full_pattern = (root / pattern).string();

  spdlog::debug("Glob: pattern={}, root={}", full_pattern, root.string());

  std::vector<std::filesystem::path> matches;
  try {
    bool recursive = std::string(pattern).find("**") != std::string::npos;
    if (recursive) {
      matches = glob::rglob(full_pattern);
    } else {
      matches = glob::glob(full_pattern);
    }
  } catch (const std::exception& e) {
    return absl::InternalError(fmt::format("glob failed: {}", e.what()));
  }

  std::vector<std::string> results;
  for (const auto& m : matches) {
    auto normal = std::filesystem::absolute(m).lexically_normal().string();
    if (!options.include_dirs) {
      std::error_code ec;
      if (std::filesystem::is_directory(m, ec)) continue;
    }
    results.push_back(normal);
  }

  std::sort(results.begin(), results.end());
  return results;
}

absl::StatusOr<std::vector<uint8_t>> LocalHost::ReadBytes(std::string_view path) {
  auto p = ResolvePath(path);
  std::ifstream file(p, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return absl::NotFoundError(fmt::format("file not found: {}", p.string()));
  }
  auto size = file.tellg();
  file.seekg(0);
  std::vector<uint8_t> content(static_cast<size_t>(size));
  file.read(reinterpret_cast<char*>(content.data()), static_cast<std::streamsize>(content.size()));
  return content;
}

absl::StatusOr<std::string> LocalHost::ReadText(std::string_view path) {
  auto p = ResolvePath(path);
  auto content = ReadFileContents(p);
  if (content.empty()) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
      return absl::NotFoundError(fmt::format("file not found: {}", p.string()));
    }
  }
  return content;
}

absl::StatusOr<std::vector<std::string>> LocalHost::ReadLines(std::string_view path, int count) {
  auto p = ResolvePath(path);
  std::ifstream file(p);
  if (!file.is_open()) {
    return absl::NotFoundError(fmt::format("file not found: {}", p.string()));
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(file, line)) {
    lines.push_back(line);
    if (count > 0 && static_cast<int>(lines.size()) >= count) break;
  }
  return lines;
}

absl::Status LocalHost::WriteBytes(std::string_view path, std::span<const uint8_t> data) {
  auto p = ResolvePath(path);
  return WriteFileContents(p, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

absl::Status LocalHost::WriteText(std::string_view path, std::string_view data) {
  return WriteFileContents(ResolvePath(path), data);
}

absl::Status LocalHost::Mkdir(std::string_view path, const MkdirOptions& options) {
  auto p = ResolvePath(path);
  std::error_code ec;
  bool created = false;

  if (options.recursive) {
    created = std::filesystem::create_directories(p, ec);
  } else {
    created = std::filesystem::create_directory(p, ec);
  }

  if (!created && !ec) {
    if (!options.exist_ok) {
      return absl::AlreadyExistsError(fmt::format("directory already exists: {}", p.string()));
    }
  } else if (ec) {
    return absl::InternalError(fmt::format("failed to create directory: {}: {}", p.string(), ec.message()));
  }

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<HostProcess>> LocalHost::Exec(std::string_view command, std::string_view cwd) {
  std::string work_dir_str;
  if (!cwd.empty()) {
    work_dir_str = std::filesystem::absolute(std::filesystem::path(cwd)).string();
  }

  reproc::process proc;
  reproc::options opts;
  opts.redirect.out.type = reproc::redirect::pipe;
  opts.redirect.err.type = reproc::redirect::pipe;
  opts.redirect.in.type = reproc::redirect::pipe;

  if (!work_dir_str.empty()) {
    opts.working_directory = work_dir_str.c_str();
  }

  std::string shell = shell_path_;
#ifdef _WIN32
  auto has_non_ascii = [](const std::string& s) {
    return std::any_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) > 127; });
  };
  if (has_non_ascii(shell)) {
    shell = "cmd.exe";
  }
#endif

  std::vector<std::string> args;
  if (shell_name_ == "cmd" || shell == "cmd.exe") {
    args = {shell, "/c", std::string(command)};
  } else {
    args = {shell, "-c", std::string(command)};
  }

  spdlog::debug("Exec: shell={}, cmd={}", shell, command);

  auto ec = proc.start(args, opts);
  if (ec) {
    return absl::InternalError(fmt::format("failed to spawn process: {}", ec.message()));
  }

  return std::make_unique<ReprocProcess>(std::move(proc));
}

absl::StatusOr<std::unique_ptr<HostProcess>> LocalHost::ExecWithEnv(
    std::vector<std::string> args, std::string_view cwd, const std::vector<std::pair<std::string, std::string>>& env) {
  std::string work_dir_str;
  if (!cwd.empty()) {
    work_dir_str = std::filesystem::absolute(std::filesystem::path(cwd)).string();
  }

  reproc::process proc;
  reproc::options opts;
  opts.redirect.out.type = reproc::redirect::pipe;
  opts.redirect.err.type = reproc::redirect::pipe;
  opts.redirect.in.type = reproc::redirect::pipe;

  if (!work_dir_str.empty()) {
    opts.working_directory = work_dir_str.c_str();
  }

  if (!env.empty()) {
    opts.env.extra = reproc::env(env);
  }

  auto ec = proc.start(args, opts);
  if (ec) {
    return absl::InternalError(fmt::format("failed to spawn process: {}", ec.message()));
  }

  return std::make_unique<ReprocProcess>(std::move(proc));
}

}  // namespace codeharness::host