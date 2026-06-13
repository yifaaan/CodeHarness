#include "host/host_path.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "host/local_host.h"

namespace host = codeharness::host;

#ifdef _WIN32
#define SEP "\\"
#else
#define SEP "/"
#endif

TEST_CASE("HostPath construction and string conversion") {
  host::HostPath p("usr/local/bin");
  CHECK_EQ(p.String(), "usr/local/bin");
}

TEST_CASE("HostPath::Name") {
  CHECK_EQ(host::HostPath("foo/bar/file.txt").Name(), "file.txt");
  CHECK_EQ(host::HostPath("just_file.txt").Name(), "just_file.txt");
}

TEST_CASE("HostPath::Parent") {
  auto parent = host::HostPath("foo/bar/baz.txt").Parent();
  std::string ps = parent.String();
  bool ok = (ps.find("foo/bar") != std::string::npos || ps.find("foo\\bar") != std::string::npos);
  CHECK(ok);
}

TEST_CASE("HostPath::IsAbsolute") {
#ifdef _WIN32
  CHECK(host::HostPath("C:\\path").IsAbsolute());
  CHECK_FALSE(host::HostPath("relative/path").IsAbsolute());
#else
  CHECK(host::HostPath("/absolute/path").IsAbsolute());
  CHECK_FALSE(host::HostPath("relative/path").IsAbsolute());
#endif
}

TEST_CASE("HostPath::Extension and Stem") {
  host::HostPath p("document.txt");
  CHECK_EQ(p.Extension(), ".txt");
  CHECK_EQ(p.Stem(), "document");
}

TEST_CASE("HostPath::Joinpath / operator/") {
  host::HostPath base("base");
  auto joined = base / "sub" / "file.txt";
  CHECK_EQ(joined.String(), "base" SEP "sub" SEP "file.txt");
}

TEST_CASE("HostPath::ExpandUser") {
  auto expanded = host::HostPath("~/documents").ExpandUser();
  CHECK_NE(expanded.String(), "~/documents");
  bool no_tilde = expanded.String().find("~") == std::string::npos;
  CHECK(no_tilde);
}

TEST_CASE("HostPath::Canonical") {
  host::HostPath p(".");
  auto canon = p.Canonical();
  CHECK(canon.IsAbsolute());
}

TEST_CASE("HostPath::RelativeTo") {
  host::HostPath base("/foo/bar");
  host::HostPath target("/foo/bar/baz/file.txt");
  auto rel = target.RelativeTo(base);
  std::string rel_str = rel.String();
  bool ok = (rel_str == "baz/file.txt" || rel_str == "baz\\file.txt");
  CHECK(ok);
}

TEST_CASE("HostPath::Resolve") {
  host::HostPath p(".");
  auto resolved = p.Resolve();
  CHECK(resolved.IsAbsolute());
}