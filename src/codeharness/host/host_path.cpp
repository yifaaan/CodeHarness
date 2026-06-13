#include "host_path.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <fmt/format.h>

#include <cstdlib>
#include <filesystem>

#include "current_host.h"

namespace codeharness::host {

namespace {

absl::StatusOr<Host*> ResolveHost(Host* host) {
  if (host) return host;
  auto* h = GetCurrentHostNoThrow();
  if (h) return h;
  return absl::InternalError("no host available");
}

}  // namespace

HostPath::HostPath(std::string_view path, std::string_view path_class) : path_(path), path_class_(path_class) {}

HostPath::HostPath(const std::filesystem::path& path, std::string_view path_class)
    : path_(path), path_class_(path_class) {}

std::string HostPath::Name() const { return path_.filename().string(); }

HostPath HostPath::Parent() const { return HostPath(path_.parent_path(), path_class_); }

std::string HostPath::String() const { return path_.string(); }

bool HostPath::IsAbsolute() const { return path_.is_absolute(); }

std::string HostPath::Extension() const { return path_.extension().string(); }

std::string HostPath::Stem() const { return path_.stem().string(); }

HostPath HostPath::operator/(std::string_view other) const { return HostPath(path_ / other, path_class_); }

HostPath HostPath::Joinpath(std::string_view other) const { return *this / other; }

HostPath HostPath::Canonical() const {
  std::error_code ec;
  auto p = std::filesystem::canonical(path_, ec);
  return HostPath(ec ? path_ : p, path_class_);
}

HostPath HostPath::RelativeTo(const HostPath& other) const {
  auto rel = std::filesystem::relative(path_, other.path_);
  return HostPath(rel, path_class_);
}

HostPath HostPath::ExpandUser() const {
  if (path_.empty() || path_.string()[0] != '~') return *this;
  const char* home = std::getenv("HOME");
  if (!home) home = std::getenv("USERPROFILE");
  if (!home) return *this;
  auto s = path_.string();
  if (s.size() == 1) return HostPath(std::filesystem::path(home), path_class_);
  return HostPath(std::filesystem::path(std::string(home) + s.substr(1)), path_class_);
}

HostPath HostPath::Resolve() const {
  std::error_code ec;
  auto p = std::filesystem::absolute(path_, ec);
  return HostPath(ec ? path_ : p.lexically_normal(), path_class_);
}

absl::StatusOr<StatResult> HostPath::Stat(Host* host) const {
  auto h = ResolveHost(host);
  if (!h.ok()) return h.status();
  return (*h)->Stat(path_.string());
}

absl::StatusOr<bool> HostPath::Exists(Host* host) const {
  auto h = ResolveHost(host);
  if (!h.ok()) return std::filesystem::exists(path_);
  auto s = (*h)->Stat(path_.string());
  if (s.ok()) return true;
  if (absl::IsNotFound(s.status())) return false;
  if (absl::IsPermissionDenied(s.status())) return false;
  return false;
}

absl::StatusOr<bool> HostPath::IsFile(Host* host) const {
  auto h = ResolveHost(host);
  if (!h.ok()) return std::filesystem::is_regular_file(path_);
  auto s = (*h)->Stat(path_.string());
  if (!s.ok()) return false;
  return (s->st_mode & 0170000) == 0100000;
}

absl::StatusOr<bool> HostPath::IsDir(Host* host) const {
  auto h = ResolveHost(host);
  if (!h.ok()) return std::filesystem::is_directory(path_);
  auto s = (*h)->Stat(path_.string());
  if (!s.ok()) return false;
  return (s->st_mode & 0170000) == 0040000;
}

absl::StatusOr<std::vector<std::string>> HostPath::Iterdir(Host* host) const {
  auto h = ResolveHost(host);
  if (!h.ok()) {
    std::vector<std::string> result;
    for (auto& e : std::filesystem::directory_iterator(path_)) {
      result.push_back(e.path().filename().string());
    }
    return result;
  }
  return (*h)->Iterdir(path_.string());
}

absl::StatusOr<std::vector<std::string>> HostPath::Glob(std::string_view pattern, Host* host) const {
  auto h = ResolveHost(host);
  if (!h.ok()) return h.status();
  return (*h)->Glob(pattern, path_.string());
}

absl::StatusOr<std::string> HostPath::ReadText(Host* host) const {
  auto h = ResolveHost(host);
  if (!h.ok()) return h.status();
  return (*h)->ReadText(path_.string());
}

absl::StatusOr<std::vector<std::string>> HostPath::ReadLines(Host* host, int count) const {
  auto h = ResolveHost(host);
  if (!h.ok()) return h.status();
  return (*h)->ReadLines(path_.string(), count);
}

absl::Status HostPath::WriteText(std::string_view data, Host* host) const {
  auto h = ResolveHost(host);
  if (!h.ok()) return h.status();
  return (*h)->WriteText(path_.string(), data);
}

absl::Status HostPath::Mkdir(const MkdirOptions& options, Host* host) const {
  auto h = ResolveHost(host);
  if (!h.ok()) return h.status();
  return (*h)->Mkdir(path_.string(), options);
}

}  // namespace codeharness::host