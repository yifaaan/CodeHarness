#include "host/environment.h"

#include <doctest/doctest.h>

#include <string>

#include "host/local_host.h"

namespace host = codeharness::host;

TEST_CASE("detect_environment returns a valid shell path") {
  auto env = host::DetectEnvironment();
  CHECK_FALSE(env.shell_path.empty());
  CHECK_FALSE(env.shell_name.empty());
}

TEST_CASE("probe_shell finds an executable") {
  auto shell = host::ProbeShell();
  CHECK_FALSE(shell.empty());
  std::error_code ec;
  bool exists = std::filesystem::exists(shell, ec);
  CHECK(exists);
}

TEST_CASE("environment knows the OS type") {
  auto env = host::DetectEnvironment();
#ifdef _WIN32
  CHECK(env.is_windows);
#else
  CHECK_FALSE(env.is_windows);
#endif
}

TEST_CASE("environment has PATH directories") {
  auto env = host::DetectEnvironment();
  CHECK_FALSE(env.path_dirs.empty());
}

TEST_CASE("shell name is one of the expected values") {
  auto env = host::DetectEnvironment();
  bool valid_name = (env.shell_name == "bash" || env.shell_name == "sh" || env.shell_name == "cmd");
  CHECK(valid_name);
}

TEST_CASE("LocalHost uses detected shell") {
  host::LocalHost host;
  auto cwd = host.GetCwd();
  CHECK(cwd.ok());
  CHECK_FALSE(cwd->empty());
}