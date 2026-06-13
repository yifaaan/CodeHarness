#pragma once

#include <absl/status/statusor.h>

#include <filesystem>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "host.h"
#include "host_process.h"

namespace codeharness::host {

class LocalHost : public Host {
 public:
  explicit LocalHost(std::string_view cwd = "");
  ~LocalHost() override = default;

  std::string PathClass() const override;
  absl::StatusOr<std::string> Normpath(std::string_view path) const override;
  absl::StatusOr<std::string> GetHome() const override;
  absl::StatusOr<std::string> GetCwd() const override;

  absl::Status Chdir(std::string_view path) override;
  absl::StatusOr<StatResult> Stat(std::string_view path, bool follow_symlinks = true) override;
  absl::StatusOr<std::vector<std::string>> Iterdir(std::string_view path) override;
  absl::StatusOr<std::vector<std::string>> Glob(std::string_view pattern, std::string_view path = "",
                                                const GlobOptions& options = {}) override;

  absl::StatusOr<std::vector<uint8_t>> ReadBytes(std::string_view path) override;
  absl::StatusOr<std::string> ReadText(std::string_view path) override;
  absl::StatusOr<std::vector<std::string>> ReadLines(std::string_view path, int count = 0) override;
  absl::Status WriteBytes(std::string_view path, std::span<const uint8_t> data) override;
  absl::Status WriteText(std::string_view path, std::string_view data) override;
  absl::Status Mkdir(std::string_view path, const MkdirOptions& options = {}) override;

  absl::StatusOr<std::unique_ptr<HostProcess>> Exec(std::string_view command, std::string_view cwd = "") override;

  absl::StatusOr<std::unique_ptr<HostProcess>> ExecWithEnv(
      std::vector<std::string> args, std::string_view cwd = "",
      const std::vector<std::pair<std::string, std::string>>& env = {}) override;

 private:
  std::filesystem::path cwd_;
  std::string shell_path_;
  std::string shell_name_;

  std::filesystem::path ResolvePath(std::string_view path) const;
};

}  // namespace codeharness::host