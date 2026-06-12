#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "codeharness/host/exec_result.h"
#include "codeharness/host/host.h"
#include "codeharness/host/stat_result.h"

namespace codeharness::host {

class LocalHost final : public Host {
 public:
  explicit LocalHost(std::filesystem::path initial_cwd);

  PathClass PathClassType() const override;
  std::filesystem::path Cwd() const override;
  absl::Status Chdir(const std::filesystem::path& path) override;
  std::filesystem::path Home() const override;
  std::filesystem::path Normpath(const std::filesystem::path& path) const override;

  absl::StatusOr<StatResult> Stat(const std::filesystem::path& path) const override;
  absl::StatusOr<std::vector<std::filesystem::path>> Iterdir(const std::filesystem::path& path) const override;
  absl::StatusOr<std::vector<std::filesystem::path>> Glob(
      const std::string& pattern, const std::filesystem::path& root, GlobOptions options = {}) const override;

  absl::StatusOr<std::string> ReadText(const std::filesystem::path& path) const override;
  absl::Status WriteText(const std::filesystem::path& path, std::string_view data) override;
  absl::Status Mkdir(const std::filesystem::path& path, MkdirOptions options = {}) override;

  absl::StatusOr<ExecResult> Exec(std::string_view command, ExecOptions options = {}) override;

 private:
  std::filesystem::path ResolvePath(const std::filesystem::path& path) const;

  std::filesystem::path cwd_;
};

}  // namespace codeharness::host
