#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "host_process.h"
#include "host_types.h"

namespace codeharness::host {

class Host {
 public:
  virtual ~Host() = default;

  virtual std::string PathClass() const = 0;
  virtual absl::StatusOr<std::string> Normpath(std::string_view path) const = 0;
  virtual absl::StatusOr<std::string> GetHome() const = 0;
  virtual absl::StatusOr<std::string> GetCwd() const = 0;

  virtual absl::Status Chdir(std::string_view path) = 0;
  virtual absl::StatusOr<StatResult> Stat(std::string_view path, bool follow_symlinks = true) = 0;
  virtual absl::StatusOr<std::vector<std::string>> Iterdir(std::string_view path) = 0;
  virtual absl::StatusOr<std::vector<std::string>> Glob(std::string_view pattern, std::string_view path = "",
                                                        const GlobOptions& options = {}) = 0;

  virtual absl::StatusOr<std::vector<uint8_t>> ReadBytes(std::string_view path) = 0;
  virtual absl::StatusOr<std::string> ReadText(std::string_view path) = 0;
  virtual absl::StatusOr<std::vector<std::string>> ReadLines(std::string_view path, int count = 0) = 0;
  virtual absl::Status WriteBytes(std::string_view path, std::span<const uint8_t> data) = 0;
  virtual absl::Status WriteText(std::string_view path, std::string_view data) = 0;
  virtual absl::Status Mkdir(std::string_view path, const MkdirOptions& options = {}) = 0;

  virtual absl::StatusOr<std::unique_ptr<HostProcess>> Exec(std::string_view command, std::string_view cwd = "") = 0;

  virtual absl::StatusOr<std::unique_ptr<HostProcess>> ExecWithEnv(
      std::vector<std::string> args, std::string_view cwd = "",
      const std::vector<std::pair<std::string, std::string>>& env = {}) = 0;
};

}  // namespace codeharness::host