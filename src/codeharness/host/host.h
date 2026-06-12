#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "codeharness/host/exec_result.h"
#include "codeharness/host/stat_result.h"

namespace codeharness::host {

enum class PathClass {
  kPosix,
  kWin32,
};

class Host {
 public:
  virtual ~Host() = default;

  virtual PathClass PathClassType() const = 0;
  virtual std::filesystem::path Cwd() const = 0;
  virtual absl::Status Chdir(const std::filesystem::path& path) = 0;
  virtual std::filesystem::path Home() const = 0;
  virtual std::filesystem::path Normpath(const std::filesystem::path& path) const = 0;

  virtual absl::StatusOr<StatResult> Stat(const std::filesystem::path& path) const = 0;
  virtual absl::StatusOr<std::vector<std::filesystem::path>> Iterdir(const std::filesystem::path& path) const = 0;
  virtual absl::StatusOr<std::vector<std::filesystem::path>> Glob(
      const std::string& pattern, const std::filesystem::path& root, GlobOptions options = {}) const = 0;

  virtual absl::StatusOr<std::string> ReadText(const std::filesystem::path& path) const = 0;
  virtual absl::Status WriteText(const std::filesystem::path& path, std::string_view data) = 0;
  virtual absl::Status Mkdir(const std::filesystem::path& path, MkdirOptions options = {}) = 0;

  virtual absl::StatusOr<ExecResult> Exec(std::string_view command, ExecOptions options = {}) = 0;
};

}  // namespace codeharness::host
