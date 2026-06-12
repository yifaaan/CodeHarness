#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "codeharness/platform/exec_result.h"
#include "codeharness/platform/stat_result.h"

namespace codeharness::platform {

// Unified filesystem and process execution abstraction.
// All operations are synchronous and return absl::StatusOr<T> or absl::Status.
//
// Each Platform instance maintains its own working directory,
// so multiple agents can have independent cwds without global state.
class Platform {
 public:
  virtual ~Platform() = default;

  // Per-instance working directory.
  virtual std::filesystem::path Cwd() const = 0;
  virtual void SetCwd(std::filesystem::path path) = 0;

  // Path operations.
  virtual std::filesystem::path Normpath(const std::filesystem::path& path) const = 0;
  virtual std::filesystem::path Gethome() const = 0;

  // Filesystem queries.
  virtual absl::StatusOr<StatResult> Stat(const std::filesystem::path& path) const = 0;
  virtual bool Exists(const std::filesystem::path& path) const = 0;
  virtual bool IsDirectory(const std::filesystem::path& path) const = 0;
  virtual bool IsRegularFile(const std::filesystem::path& path) const = 0;
  virtual absl::StatusOr<std::vector<std::filesystem::path>> Iterdir(const std::filesystem::path& path) const = 0;
  virtual absl::StatusOr<std::vector<std::filesystem::path>> Glob(const std::string& pattern,
                                                                  const std::filesystem::path& root = {}) const = 0;

  // File I/O.
  virtual absl::StatusOr<std::string> ReadText(const std::filesystem::path& path) const = 0;
  virtual absl::Status WriteText(const std::filesystem::path& path, const std::string& data) = 0;

  // Directory creation.
  virtual absl::Status Mkdir(const std::filesystem::path& path, MkdirOptions options = {}) = 0;

  // Process execution.
  virtual absl::StatusOr<ExecResult> Exec(const std::string& command, ExecOptions options = {}) = 0;
};

}  // namespace codeharness::platform
