#include "codeharness/platform/local_platform.h"

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
#endif

#include "codeharness/core/paths.h"
#include "codeharness/core/shell.h"

namespace codeharness::platform {

// ============================================================
// Constants
// ============================================================

static constexpr std::size_t kMaxGlobResults = 200;
static constexpr std::size_t kMaxOutputLength = 12000;
static constexpr std::string_view kOutputTruncatedMarker = "\n...[output truncated, too long]...";

// ============================================================
// Helpers (file-internal)
// ============================================================

namespace {

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

void DrainAvailableOutput(reproc::process& process, std::string& output, bool& truncated) {
  std::array<std::uint8_t, 4096> buf;
  while (true) {
    auto [bytes_read, error] = process.read(reproc::stream::out, buf.data(), buf.size());

    if (!error && bytes_read > 0) {
      if (bytes_read > buf.size()) {
        spdlog::warn("platform exec read reported {} bytes for a {} byte buffer", bytes_read, buf.size());
        truncated = true;
        break;
      }

      AppendOutput(output, buf.data(), bytes_read, truncated);
      continue;
    }

    if (error && error != std::errc::resource_unavailable_try_again && error != std::errc::operation_would_block &&
        error != std::errc::broken_pipe) {
      spdlog::warn("failed to read exec output: {}", error.message());
    }

    break;
  }
}

struct FileIdentity {
  std::uint64_t device_id = 0;
  std::uint64_t inode_number = 0;
};

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
  struct stat st{};
  if (::stat(path.c_str(), &st) == 0) {
    identity.device_id = static_cast<std::uint64_t>(st.st_dev);
    identity.inode_number = static_cast<std::uint64_t>(st.st_ino);
  }
#endif
  return identity;
}

}  // namespace

// ============================================================
// Construction
// ============================================================

LocalPlatform::LocalPlatform(std::filesystem::path initial_cwd) : cwd_(std::move(initial_cwd)) {}

// ============================================================
// Cwd management
// ============================================================

std::filesystem::path LocalPlatform::Cwd() const { return cwd_; }

void LocalPlatform::SetCwd(std::filesystem::path path) {
  std::error_code ec;
  auto resolved = std::filesystem::weakly_canonical(path, ec);
  cwd_ = ec ? std::move(path) : std::move(resolved);
}

// ============================================================
// Path operations
// ============================================================

std::filesystem::path LocalPlatform::Normpath(const std::filesystem::path& path) const {
  std::error_code ec;
  auto resolved = std::filesystem::weakly_canonical(path, ec);
  return ec ? path : resolved;
}

std::filesystem::path LocalPlatform::Gethome() const {
  auto home = HomeDirectory();
  return home.value_or(std::filesystem::current_path());
}

// ============================================================
// Filesystem queries
// ============================================================

absl::StatusOr<StatResult> LocalPlatform::Stat(const std::filesystem::path& path) const {
  std::error_code ec;

  auto symlink_status = std::filesystem::symlink_status(path, ec);
  if (ec) {
    return absl::NotFoundError("failed to stat: " + path.string() + ": " + ec.message());
  }

  auto regular_status = std::filesystem::status(path, ec);
  if (ec) {
    return absl::InternalError("failed to stat: " + path.string() + ": " + ec.message());
  }

  StatResult result;
  result.is_symlink = std::filesystem::is_symlink(symlink_status);
  result.is_directory = std::filesystem::is_directory(regular_status);
  result.is_regular_file = std::filesystem::is_regular_file(regular_status);

  if (result.is_regular_file) {
    result.size = std::filesystem::file_size(path, ec);
    if (ec) {
      result.size = 0;
    }
  }

  auto ftime = std::filesystem::last_write_time(path, ec);
  if (!ec) {
    result.modified_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
  }

  auto identity = GetFileIdentity(path);
  result.device_id = identity.device_id;
  result.inode_number = identity.inode_number;

  return result;
}

bool LocalPlatform::Exists(const std::filesystem::path& path) const { return std::filesystem::exists(path); }

bool LocalPlatform::IsDirectory(const std::filesystem::path& path) const { return std::filesystem::is_directory(path); }

bool LocalPlatform::IsRegularFile(const std::filesystem::path& path) const {
  return std::filesystem::is_regular_file(path);
}

absl::StatusOr<std::vector<std::filesystem::path>> LocalPlatform::Iterdir(const std::filesystem::path& path) const {
  std::error_code ec;
  if (!std::filesystem::is_directory(path, ec)) {
    return absl::NotFoundError("not a directory: " + path.string());
  }

  std::vector<std::filesystem::path> entries;
  for (auto it = std::filesystem::directory_iterator(path, ec); it != std::filesystem::directory_iterator(); ++it) {
    if (it->path().filename() == "." || it->path().filename() == "..") {
      continue;
    }
    entries.push_back(it->path());
  }

  std::sort(entries.begin(), entries.end());
  return entries;
}

// ============================================================
// Glob
// ============================================================

absl::StatusOr<std::vector<std::filesystem::path>> LocalPlatform::Glob(const std::string& pattern,
                                                                       const std::filesystem::path& root) const {
  auto search_root = root.empty() ? cwd_ : root;

  std::error_code ec;
  if (!std::filesystem::is_directory(search_root, ec)) {
    return absl::NotFoundError("glob root is not a directory: " + search_root.string());
  }

  // Track visited (dev, ino) pairs for symlink cycle detection.
  std::unordered_set<std::uint64_t> visited;

  std::vector<std::filesystem::path> results;
  std::unordered_set<std::string> emitted;

  auto append_matches = [&](const std::vector<std::filesystem::path>& matches) {
    for (const auto& match : matches) {
      if (results.size() >= kMaxGlobResults) {
        return;
      }

      // Cycle detection.
      auto identity = GetFileIdentity(match);
      if (identity.device_id != 0 || identity.inode_number != 0) {
        std::uint64_t key = identity.device_id * 131 + identity.inode_number;
        if (!visited.insert(key).second) {
          continue;  // Already visited (symlink loop).
        }
      }

      std::error_code rel_ec;
      auto relative = std::filesystem::relative(match, search_root, rel_ec);
      if (rel_ec) {
        continue;
      }

      auto result = relative.generic_string();
      if (!emitted.insert(result).second) {
        continue;  // Duplicate.
      }

      results.push_back(std::move(relative));
    }
  };

  if (pattern.starts_with("**/")) {
    append_matches(glob::glob(search_root.string() + '/' + pattern.substr(3)));
  }

  append_matches(glob::rglob(search_root.string() + '/' + pattern));

  return results;
}

// ============================================================
// File I/O
// ============================================================

absl::StatusOr<std::string> LocalPlatform::ReadText(const std::filesystem::path& path) const {
  std::ifstream file{path, std::ios::binary};
  if (!file) {
    return absl::NotFoundError("failed to open file: " + path.string());
  }

  std::ostringstream oss;
  oss << file.rdbuf();

  if (!file.good() && !file.eof()) {
    return absl::InternalError("failed to read file: " + path.string());
  }

  return oss.str();
}

absl::Status LocalPlatform::WriteText(const std::filesystem::path& path, const std::string& data) {
  auto tmp_path = path;
  tmp_path += ".tmp";

  std::ofstream file{tmp_path, std::ios::binary};
  if (!file) {
    return absl::InternalError("failed to create temp file: " + tmp_path.string());
  }

  file << data;
  file.flush();

  if (!file.good()) {
    std::error_code ignored;
    std::filesystem::remove(tmp_path, ignored);
    return absl::InternalError("failed to write file content");
  }

  file.close();

  std::error_code ec;
  std::filesystem::rename(tmp_path, path, ec);
  if (ec) {
    std::error_code ignored;
    std::filesystem::remove(tmp_path, ignored);
    return absl::InternalError("failed to rename temp file: " + ec.message());
  }

  return absl::OkStatus();
}

// ============================================================
// Directory creation
// ============================================================

absl::Status LocalPlatform::Mkdir(const std::filesystem::path& path, MkdirOptions options) {
  std::error_code ec;

  if (options.parents) {
    std::filesystem::create_directories(path, ec);
  } else {
    std::filesystem::create_directory(path, ec);
  }

  if (ec) {
    if (options.exist_ok && std::filesystem::is_directory(path)) {
      return absl::OkStatus();
    }
    return absl::InternalError("failed to create directory: " + path.string() + ": " + ec.message());
  }

  return absl::OkStatus();
}

// ============================================================
// Process execution
// ============================================================

absl::StatusOr<ExecResult> LocalPlatform::Exec(const std::string& command, ExecOptions options) {
  spdlog::info("platform exec: {}", command);

  auto argv = DefaultShellCommandArgv(command);

  reproc::process process;
  reproc::options opts{};

  auto work_dir = options.working_directory.empty() ? cwd_.string() : options.working_directory;
  auto cwd_str = work_dir;
  opts.working_directory = cwd_str.c_str();
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
    DrainAvailableOutput(process, result.stdout_output, stdout_truncated);

    // Read stderr separately.
    std::array<std::uint8_t, 4096> err_buf;
    while (true) {
      auto [bytes_read, err] = process.read(reproc::stream::err, err_buf.data(), err_buf.size());
      if (!err && bytes_read > 0) {
        AppendOutput(result.stderr_output, err_buf.data(), bytes_read, stderr_truncated);
        continue;
      }
      break;
    }

    auto [exit_status, wait_error] = process.wait(reproc::milliseconds{0});
    if (!wait_error) {
      DrainAvailableOutput(process, result.stdout_output, stdout_truncated);
      result.exit_status = exit_status;

      // Drain remaining stderr.
      while (true) {
        auto [bytes_read, err] = process.read(reproc::stream::err, err_buf.data(), err_buf.size());
        if (!err && bytes_read > 0) {
          AppendOutput(result.stderr_output, err_buf.data(), bytes_read, stderr_truncated);
          continue;
        }
        break;
      }

      break;
    }

    if (wait_error != std::errc::timed_out) {
      return absl::InternalError("failed to wait for process: " + wait_error.message());
    }

    if (std::chrono::steady_clock::now() >= deadline) {
      DrainAvailableOutput(process, result.stdout_output, stdout_truncated);

      // Two-phase kill: SIGTERM → wait 5s → SIGKILL.
      if (auto error = process.kill()) {
        spdlog::warn("failed to kill timed-out process: {}", error.message());
      }
      process.wait(reproc::milliseconds{5000});

      DrainAvailableOutput(process, result.stdout_output, stdout_truncated);
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

}  // namespace codeharness::platform
