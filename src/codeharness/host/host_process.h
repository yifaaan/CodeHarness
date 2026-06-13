#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness::host {

class HostProcess {
 public:
  virtual ~HostProcess() = default;

  virtual absl::Status WriteStdin(std::string_view data) = 0;
  virtual absl::Status CloseStdin() = 0;
  virtual absl::StatusOr<std::string> ReadStdout() = 0;
  virtual absl::StatusOr<std::string> ReadStderr() = 0;
  virtual absl::StatusOr<int> Pid() const = 0;
  virtual absl::StatusOr<int> ExitCode() const = 0;
  virtual absl::StatusOr<int> Wait() = 0;
  virtual absl::Status Kill(const std::string& signal = "SIGTERM") = 0;
};

}  // namespace codeharness::host