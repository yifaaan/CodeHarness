#include "codeharness/host/local_host.h"

#include <glob/glob.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <reproc++/reproc.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#include "codeharness/core/paths.h"
#include "codeharness/core/shell.h"

namespace codeharness::host {

namespace {

constexpr std::size_t kMaxOutputLength = 12000;
constexpr std::string_view kOutputTruncatedMarker = "\n...[output truncated, too long]...";

struct FileIdentity {
  std::uint64_t device_id = 0;
  std::uint64_t inode_number = 0;
};

void AppendOutput(std::string& output, const std::uint8_t* data, std::size_t size, bool& truncated) {
  if (truncated || size == 0) {
    return;
  }

  const auto remaining = kMaxOutputLength > output.size() ? kMaxOutputLength - output.size() : std::size_t{0};
  if (size > remaining) {
    output.append(reinterpret_cast<const char*>(data), remaining);
    truncated = true;
    return;
  }

  output.append(reinterpret_cast<const char*>(data), size);
}

void AppendTruncationMarker(std::string& output, bool truncated) {
  if (truncated) {
    output += kOutputTruncatedMarker;
  }
}

void DrainAvailableOutput(reproc::process& process, reproc::stream stream, std::string& output, bool& truncated) {
  std::array<std::uint8_t, 4096> buffer{};
  while (true) {
    auto [bytes_read, error] = process.read(stream, buffer.data(), buffer.size());

    if (!error && bytes_read > 0) {
      if (bytes_read > buffer.size()) {
        spdlog::warn("host exec read reported {} bytes for a {} byte buffer", bytes_read, buffer.size());
        truncated = true;
        break;
      }

      AppendOutput(output, buffer.data(), bytes_read, truncated);
      continue;
    }

    if (error && error != std::errc::resource_unavailable_try_again && error != std::errc::operation_would_block &&
        error != std::errc::broken_pipe) {
      spdlog::warn("failed to read exec output: {}", error.message());
    }

    break;
  }
}

FileIdentity GetFileIdentity(const std::filesystem::path& path) {
  FileIdentity identity;
#ifdef _WIN32
  HANDLE handle = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return identity;
  }
  BY_HANDLE_FILE_INFORMATION info{};
  if (GetFileInformationByHandle(handle, &info)) {
    identity.device_id = info.dwVolumeSerialNumber;
    identity.inode_number = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
  }
  CloseHandle(handle);
#else
  struct stat st {};
  if (::stat(path.c_str(), &st) == 0) {
    identity.device_id = static_cast<std::uint64_t>(st.st_dev);
    identity.inode_number = static_cast<std::uint64_t>(st.st_ino);
  }
#endif
  return identity;
}

std::uint64_t IdentityKey(const FileIdentity& identity) {
  return identity.device_id * 131 + identity.inode_number;
}

bool IsUnderMaxDepth(const std::filesystem::path& relative, int max_depth) {
  if (max_depth < 0) {
    return true;
  }

  const auto text = relative.generic_string();
  if (text.empty() || text == ".") {
    return true;
  }

  auto depth = 0;
  for (const auto& part : relative) {
    if (part == ".") {
      continue;
    }
    ++depth;
  }
  return depth <= max_depth;
}

}  // namespace

LocalHost::LocalHost(std::filesystem::path initial_cwd) : cwd_(std::move(initial_cwd)) {}

PathClass LocalHost::PathClassType() const {
#ifdef _WIN32
  return PathClass::kWin32;
#else
  return PathClass::kPosix;
#endif
}

std::filesystem::path LocalHost::Cwd() const { return cwd_; }

absl::Status LocalHost::Chdir(const std::filesystem::path& path) {
  const auto resolved = ResolvePath(path);
  std::error_code ec;
  if (!std::filesystem::is_directory(resolved, ec)) {
    return absl::NotFoundError("not a directory: " + resolved.string());
  }

  auto canonical = std::filesystem::weakly_canonical(resolved, ec);
  cwd_ = ec ? resolved : std::move(canonical);
  return absl::OkStatus();
}

std::filesystem::path LocalHost::Home() const {
  auto home_dir = HomeDirectory();
  return home_dir.value_or(std::filesystem::current_path());
}

std::filesystem::path LocalHost::Normpath(const std::filesystem::path& path) const {
  std::error_code ec;
  auto resolved = std::filesystem::weakly_canonical(ResolvePath(path), ec);
  return ec ? ResolvePath(path).lexically_normal() : resolved;
}

absl::StatusOr<StatResult> LocalHost::Stat(const std::filesystem::path& path) const {
  const auto resolved = ResolvePath(path);
  std::error_code ec;

  auto symlink_status = std::filesystem::symlink_status(resolved, ec);
  if (ec) {
    return absl::NotFoundError("failed to stat: " + resolved.string() + ": " + ec.message());
  }

  auto regular_status = std::filesystem::status(resolved, ec);
  if (ec) {
    return absl::InternalError("failed to stat: " + resolved.string() + ": " + ec.message());
  }

  StatResult result;
  result.is_symlink = std::filesystem::is_symlink(symlink_status);
  result.is_directory = std::filesystem::is_directory(regular_status);
  result.is_regular_file = std::filesystem::is_regular_file(regular_status);

  if (result.is_regular_file) {
    result.size = std::filesystem::file_size(resolved, ec);
    if (ec) {
      result.size = 0;
    }
  }

  auto file_time = std::filesystem::last_write_time(resolved, ec);
  if (!ec) {
    result.modified_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
  }

  auto identity = GetFileIdentity(resolved);
  result.device_id = identity.device_id;
  result.inode_number = identity.inode_number;
  return result;
}

absl::StatusOr<std::vector<std::filesystem::path>> LocalHost::Iterdir(const std::filesystem::path& path) const {
  const auto resolved = ResolvePath(path);
  std::error_code ec;
  if (!std::filesystem::is_directory(resolved, ec)) {
    return absl::NotFoundError("not a directory: " + resolved.string());
  }

  std::vector<std::filesystem::path> entries;
  for (auto it = std::filesystem::directory_iterator(resolved, ec); it != std::filesystem::directory_iterator(); ++it) {
    if (ec) {
      return absl::InternalError("failed to iterate directory: " + resolved.string() + ": " + ec.message());
    }
    if (it->path().filename() == "." || it->path().filename() == "..") {
      continue;
    }
    entries.push_back(it->path());
  }

  std::sort(entries.begin(), entries.end());
  return entries;
}

absl::StatusOr<std::vector<std::filesystem::path>> LocalHost::Glob(const std::string& pattern,
                                                                   const std::filesystem::path& root,
                                                                   GlobOptions options) const {
  const auto search_root = root.empty() ? cwd_ : ResolvePath(root);

  std::error_code ec;
  if (!std::filesystem::is_directory(search_root, ec)) {
    return absl::NotFoundError("glob root is not a directory: " + search_root.string());
  }

  std::vector<std::filesystem::path> matches;
  if (pattern.starts_with("**/")) {
    auto shallow_matches = glob::glob((search_root / pattern.substr(3)).string());
    matches.insert(matches.end(), shallow_matches.begin(), shallow_matches.end());
  }
  auto recursive_matches = glob::rglob((search_root / pattern).string());
  matches.insert(matches.end(), recursive_matches.begin(), recursive_matches.end());

  std::vector<std::filesystem::path> results;
  std::unordered_set<std::string> emitted;
  std::unordered_set<std::uint64_t> visited;

  for (const auto& match : matches) {
    if (results.size() >= options.max_results) {
      break;
    }

    std::error_code status_ec;
    if (!options.include_directories && std::filesystem::is_directory(match, status_ec)) {
      continue;
    }

    auto identity = GetFileIdentity(match);
    if (identity.device_id != 0 || identity.inode_number != 0) {
      const auto key = IdentityKey(identity);
      if (!visited.insert(key).second) {
        continue;
      }
    }

    std::error_code relative_ec;
    auto relative = std::filesystem::relative(match, search_root, relative_ec);
    if (relative_ec || !IsUnderMaxDepth(relative, options.max_depth)) {
      continue;
    }

    auto key = relative.generic_string();
    if (!emitted.insert(key).second) {
      continue;
    }

    results.push_back(std::move(relative));
  }

  return results;
}

absl::StatusOr<std::string> LocalHost::ReadText(const std::filesystem::path& path) const {
  const auto resolved = ResolvePath(path);
  std::ifstream file{resolved, std::ios::binary};
  if (!file) {
    return absl::NotFoundError("failed to open file: " + resolved.string());
  }

  std::ostringstream output;
  output << file.rdbuf();
  if (!file.good() && !file.eof()) {
    return absl::InternalError("failed to read file: " + resolved.string());
  }

  return output.str();
}

absl::Status LocalHost::WriteText(const std::filesystem::path& path, std::string_view data) {
  const auto resolved = ResolvePath(path);
  auto tmp_path = resolved;
  tmp_path += ".tmp";

  std::ofstream file{tmp_path, std::ios::binary};
  if (!file) {
    return absl::InternalError("failed to create temp file: " + tmp_path.string());
  }

  file.write(data.data(), static_cast<std::streamsize>(data.size()));
  file.flush();

  if (!file.good()) {
    std::error_code ignored;
    std::filesystem::remove(tmp_path, ignored);
    return absl::InternalError("failed to write file content");
  }

  file.close();

  std::error_code ec;
#ifdef _WIN32
  std::filesystem::remove(resolved, ec);
  if (ec) {
    std::error_code ignored;
    std::filesystem::remove(tmp_path, ignored);
    return absl::InternalError("failed to replace existing file: " + ec.message());
  }
#endif
  std::filesystem::rename(tmp_path, resolved, ec);
  if (ec) {
    std::error_code ignored;
    std::filesystem::remove(tmp_path, ignored);
    return absl::InternalError("failed to rename temp file: " + ec.message());
  }

  return absl::OkStatus();
}

absl::Status LocalHost::Mkdir(const std::filesystem::path& path, MkdirOptions options) {
  const auto resolved = ResolvePath(path);
  std::error_code ec;

  if (options.recursive) {
    std::filesystem::create_directories(resolved, ec);
  } else {
    std::filesystem::create_directory(resolved, ec);
  }

  if (ec) {
    if (options.exist_ok && std::filesystem::is_directory(resolved)) {
      return absl::OkStatus();
    }
    return absl::InternalError("failed to create directory: " + resolved.string() + ": " + ec.message());
  }

  if (!options.exist_ok && std::filesystem::is_directory(resolved)) {
    return absl::AlreadyExistsError("directory already exists: " + resolved.string());
  }

  return absl::OkStatus();
}

absl::StatusOr<ExecResult> LocalHost::Exec(std::string_view command, ExecOptions options) {
  spdlog::info("host exec: {}", command);

  auto argv = DefaultShellCommandArgv(std::string{command});
  reproc::process process;
  reproc::options opts{};

  const auto work_dir = options.working_directory.empty() ? cwd_ : ResolvePath(options.working_directory);
  const auto cwd_string = work_dir.string();
  opts.working_directory = cwd_string.c_str();
  opts.redirect.in.type = reproc::redirect::discard;
  opts.redirect.out.type = reproc::redirect::pipe;
  opts.redirect.err.type = reproc::redirect::pipe;
  opts.nonblocking = true;

  if (auto error = process.start(argv, opts)) {
    return absl::InternalError("failed to start process: " + error.message());
  }

  ExecResult result;
  bool stdout_truncated = false;
  bool stderr_truncated = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{options.timeout_seconds};

  while (true) {
    DrainAvailableOutput(process, reproc::stream::out, result.stdout_output, stdout_truncated);
    DrainAvailableOutput(process, reproc::stream::err, result.stderr_output, stderr_truncated);

    auto [exit_status, wait_error] = process.wait(reproc::milliseconds{0});
    if (!wait_error) {
      DrainAvailableOutput(process, reproc::stream::out, result.stdout_output, stdout_truncated);
      DrainAvailableOutput(process, reproc::stream::err, result.stderr_output, stderr_truncated);
      result.exit_status = exit_status;
      break;
    }

    if (wait_error != std::errc::timed_out) {
      return absl::InternalError("failed to wait for process: " + wait_error.message());
    }

    if (std::chrono::steady_clock::now() >= deadline) {
      DrainAvailableOutput(process, reproc::stream::out, result.stdout_output, stdout_truncated);
      DrainAvailableOutput(process, reproc::stream::err, result.stderr_output, stderr_truncated);

      if (auto error = process.kill()) {
        spdlog::warn("failed to kill timed-out process: {}", error.message());
      }
      process.wait(reproc::milliseconds{5000});

      DrainAvailableOutput(process, reproc::stream::out, result.stdout_output, stdout_truncated);
      DrainAvailableOutput(process, reproc::stream::err, result.stderr_output, stderr_truncated);
      result.timed_out = true;
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }

  AppendTruncationMarker(result.stdout_output, stdout_truncated);
  AppendTruncationMarker(result.stderr_output, stderr_truncated);

  if (result.timed_out) {
    if (!result.stdout_output.empty() && result.stdout_output.back() != '\n') {
      result.stdout_output += '\n';
    }
    result.stdout_output += "[command timed out after " + std::to_string(options.timeout_seconds) + " seconds]";
  }

  return result;
}

std::filesystem::path LocalHost::ResolvePath(const std::filesystem::path& path) const {
  if (path.is_absolute()) {
    return path;
  }
  return cwd_ / path;
}

}  // namespace codeharness::host
