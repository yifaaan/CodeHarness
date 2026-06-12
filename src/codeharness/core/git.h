#pragma once

#include <git2.h>

#include <array>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace codeharness {

struct GitRuntime {
  GitRuntime() { git_libgit2_init(); }
  ~GitRuntime() { git_libgit2_shutdown(); }

  GitRuntime(const GitRuntime&) = delete;
  GitRuntime& operator=(const GitRuntime&) = delete;
};

inline absl::StatusOr<std::string> GitBlobHashHex(std::string_view text) {
  GitRuntime runtime;

  git_oid oid{};
  if (git_odb_hash(&oid, text.data(), text.size(), GIT_OBJECT_BLOB) != 0) {
    return absl::InternalError("failed to hash content");
  }

  std::array<char, GIT_OID_SHA1_HEXSIZE + 1> buffer{};
  git_oid_tostr(buffer.data(), buffer.size(), &oid);
  return std::string{buffer.data()};
}

}  // namespace codeharness
