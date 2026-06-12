#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "codeharness/platform/exec_result.h"
#include "codeharness/platform/platform.h"
#include "codeharness/platform/stat_result.h"

namespace codeharness::platform {

class LocalPlatform final : public Platform {
 public:
  explicit LocalPlatform(std::filesystem::path initial_cwd);

  std::filesystem::path Cwd() const override;
  void SetCwd(std::filesystem::path path) override;

  std::filesystem::path Normpath(const std::filesystem::path& path) const override;
  std::filesystem::path Gethome() const override;

  absl::StatusOr<StatResult> Stat(const std::filesystem::path& path) const override;
  bool Exists(const std::filesystem::path& path) const override;
  bool IsDirectory(const std::filesystem::path& path) const override;
  bool IsRegularFile(const std::filesystem::path& path) const override;
  absl::StatusOr<std::vector<std::filesystem::path>> Iterdir(const std::filesystem::path& path) const override;
  absl::StatusOr<std::vector<std::filesystem::path>> Glob(const std::string& pattern,
                                                          const std::filesystem::path& root = {}) const override;

  absl::StatusOr<std::string> ReadText(const std::filesystem::path& path) const override;
  absl::Status WriteText(const std::filesystem::path& path, const std::string& data) override;
  absl::Status Mkdir(const std::filesystem::path& path, MkdirOptions options = {}) override;

  absl::StatusOr<ExecResult> Exec(const std::string& command, ExecOptions options = {}) override;

 private:
  std::filesystem::path cwd_;
};

}  // namespace codeharness::platform
