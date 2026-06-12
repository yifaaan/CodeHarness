#pragma once

// Convenience header: brings in absl::Status and absl::StatusOr.
// Use absl's own error factories directly:
//   absl::InternalError("msg")
//   absl::NotFoundError("msg")
//   absl::InvalidArgumentError("msg")
//   absl::CancelledError("msg")
//   absl::AlreadyExistsError("msg")
//   absl::DeadlineExceededError("msg")
//   absl::UnavailableError("msg")
//   absl::FailedPreconditionError("msg")
//   etc.

#include "absl/status/status.h"
#include "absl/status/statusor.h"
