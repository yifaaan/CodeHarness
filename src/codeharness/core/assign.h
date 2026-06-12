#pragma once

#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace codeharness {

// Unpacks absl::StatusOr<Value> into target.
// On failure, propagates the error as absl::Status.
template <typename Target, typename Value>
absl::Status Assign(Target& target, absl::StatusOr<Value>&& source) {
  if (!source.ok()) {
    return source.status();
  }
  target = std::move(*source);
  return absl::OkStatus();
}

}  // namespace codeharness
