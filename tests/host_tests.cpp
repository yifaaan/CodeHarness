#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "codeharness/host/exec_result.h"
#include "codeharness/host/host.h"
#include "codeharness/host/local_host.h"
#include "codeharness/host/stat_result.h"

namespace fs = std::filesystem;

namespace codeharness::host {

namespace {

fs::path MakeTempDir(std::string_view suffix = "default") {
  auto dir = fs::temp_directory_path() / ("codeharness_host_test_" + std::string{suffix});
  std::error_code ignored;
  fs::remove_all(dir, ignored);
  fs::create_directories(dir);
  return dir;
}

}  // namespace

TEST_CASE("Host cwd returns initial value") {
  auto temp_dir = MakeTempDir("cwd");
  LocalHost host(temp_dir);
  CHECK(host.Cwd() == temp_dir);
}

TEST_CASE("Host chdir canonicalizes path without mutating global cwd") {
  auto temp_dir = MakeTempDir("chdir");
  auto previous_global = fs::current_path();
  auto sub = temp_dir / "subdir";
  fs::create_directory(sub);

  LocalHost host(temp_dir);
  REQUIRE(host.Chdir("subdir").ok());
  CHECK(host.Cwd() == fs::canonical(sub));
  CHECK(fs::current_path() == previous_global);
}

TEST_CASE("Host path_class matches current platform") {
  LocalHost host(MakeTempDir("path-class"));
#ifdef _WIN32
  CHECK(host.PathClassType() == PathClass::kWin32);
#else
  CHECK(host.PathClassType() == PathClass::kPosix);
#endif
}

TEST_CASE("Host normpath resolves dot segments against cwd") {
  auto temp_dir = MakeTempDir("normpath");
  LocalHost host(temp_dir);
  auto resolved = host.Normpath(fs::path{"."} / "..");
  CHECK(resolved == fs::canonical(temp_dir).parent_path());
}

TEST_CASE("Host home returns non-empty path") {
  LocalHost host(MakeTempDir("home"));
  CHECK_FALSE(host.Home().empty());
}

TEST_CASE("Host iterdir lists sorted contents") {
  auto temp_dir = MakeTempDir("iterdir");
  std::ofstream{temp_dir / "b.txt"} << "2";
  std::ofstream{temp_dir / "a.txt"} << "1";
  std::ofstream{temp_dir / "c.txt"} << "3";

  LocalHost host(temp_dir);
  auto result = host.Iterdir(".");
  REQUIRE(result.ok());
  REQUIRE(result->size() == 3);
  CHECK(result->at(0).filename() == "a.txt");
  CHECK(result->at(1).filename() == "b.txt");
  CHECK(result->at(2).filename() == "c.txt");
}

TEST_CASE("Host iterdir fails for non-directory") {
  auto temp_dir = MakeTempDir("iterdir-nondir");
  std::ofstream{temp_dir / "file.txt"} << "x";

  LocalHost host(temp_dir);
  auto result = host.Iterdir("file.txt");
  CHECK_FALSE(result.ok());
}

TEST_CASE("Host read_text reads exact content") {
  auto temp_dir = MakeTempDir("read");
  std::ofstream{temp_dir / "read.txt", std::ios::binary} << "hello\nworld";

  LocalHost host(temp_dir);
  auto result = host.ReadText("read.txt");
  REQUIRE(result.ok());
  CHECK(*result == "hello\nworld");
}

TEST_CASE("Host read_text fails for missing file") {
  LocalHost host(MakeTempDir("read-missing"));
  auto result = host.ReadText("missing.txt");
  CHECK_FALSE(result.ok());
}

TEST_CASE("Host write_text creates and overwrites file") {
  auto temp_dir = MakeTempDir("write");
  LocalHost host(temp_dir);

  REQUIRE(host.WriteText("write.txt", "v1").ok());
  REQUIRE(host.WriteText("write.txt", "v2").ok());

  auto read_back = host.ReadText("write.txt");
  REQUIRE(read_back.ok());
  CHECK(*read_back == "v2");
}

TEST_CASE("Host mkdir supports recursive creation and exist_ok") {
  auto temp_dir = MakeTempDir("mkdir");
  LocalHost host(temp_dir);

  REQUIRE(host.Mkdir(fs::path{"a"} / "b" / "c").ok());
  CHECK(fs::is_directory(temp_dir / "a" / "b" / "c"));
  CHECK(host.Mkdir(fs::path{"a"} / "b" / "c").ok());
}

TEST_CASE("Host mkdir fails when exist_ok is false for existing directory") {
  auto temp_dir = MakeTempDir("mkdir-exists");
  LocalHost host(temp_dir);
  REQUIRE(host.Mkdir("existing").ok());

  auto status = host.Mkdir("existing", MkdirOptions{.recursive = true, .exist_ok = false});
  CHECK_FALSE(status.ok());
}

TEST_CASE("Host stat on file and directory") {
  auto temp_dir = MakeTempDir("stat");
  std::ofstream{temp_dir / "stat.txt"} << "12345";

  LocalHost host(temp_dir);
  auto file = host.Stat("stat.txt");
  REQUIRE(file.ok());
  CHECK(file->is_regular_file);
  CHECK_FALSE(file->is_directory);
  CHECK(file->size == 5);

  auto dir = host.Stat(".");
  REQUIRE(dir.ok());
  CHECK(dir->is_directory);
  CHECK_FALSE(dir->is_regular_file);
}

TEST_CASE("Host stat fails for missing path") {
  LocalHost host(MakeTempDir("stat-missing"));
  auto result = host.Stat("missing.txt");
  CHECK_FALSE(result.ok());
}

TEST_CASE("Host glob matches files relative to root") {
  auto temp_dir = MakeTempDir("glob");
  std::ofstream{temp_dir / "test1.txt"} << "a";
  std::ofstream{temp_dir / "test2.txt"} << "b";
  std::ofstream{temp_dir / "other.md"} << "c";

  LocalHost host(temp_dir);
  auto result = host.Glob("test*.txt", ".");
  REQUIRE(result.ok());
  CHECK(result->size() == 2);
  CHECK(std::all_of(result->begin(), result->end(), [](const fs::path& path) { return path.is_relative(); }));
}

TEST_CASE("Host glob respects max_results") {
  auto temp_dir = MakeTempDir("glob-max");
  std::ofstream{temp_dir / "a.txt"} << "a";
  std::ofstream{temp_dir / "b.txt"} << "b";
  std::ofstream{temp_dir / "c.txt"} << "c";

  LocalHost host(temp_dir);
  auto result = host.Glob("*.txt", ".", GlobOptions{.include_directories = true, .max_depth = -1, .max_results = 2});
  REQUIRE(result.ok());
  CHECK(result->size() == 2);
}

TEST_CASE("Host glob does not loop on symlink cycles") {
  auto temp_dir = MakeTempDir("glob-cycle");
  auto child = temp_dir / "child";
  fs::create_directories(child);
  std::ofstream{child / "file.txt"} << "x";

  std::error_code link_error;
  fs::create_directory_symlink(temp_dir, child / "loop", link_error);

  LocalHost host(temp_dir);
  auto result = host.Glob("**/*.txt", ".");
  REQUIRE(result.ok());
  CHECK(result->size() >= 1);
  CHECK(result->size() < 10);
}

TEST_CASE("Host exec runs command and captures stdout") {
  LocalHost host(MakeTempDir("exec"));
  ExecOptions options;
  options.timeout_seconds = 30;
  auto result = host.Exec("echo hello", options);
  REQUIRE(result.ok());
  CHECK(result->exit_status == 0);
  CHECK(result->stdout_output.find("hello") != std::string::npos);
}

TEST_CASE("Host exec returns non-zero exit status") {
  LocalHost host(MakeTempDir("exec-nonzero"));
  ExecOptions options;
  options.timeout_seconds = 30;
#ifdef _WIN32
  auto result = host.Exec("cmd.exe /c exit 7", options);
#else
  auto result = host.Exec("exit 7", options);
#endif
  REQUIRE(result.ok());
  CHECK(result->exit_status != 0);
}

TEST_CASE("Host exec supports working directory override") {
  auto temp_dir = MakeTempDir("exec-cwd");
  auto subdir = temp_dir / "subdir";
  fs::create_directory(subdir);
  LocalHost host(temp_dir);

  ExecOptions options;
  options.timeout_seconds = 30;
  options.working_directory = subdir;
#ifdef _WIN32
  auto result = host.Exec("cd", options);
#else
  auto result = host.Exec("pwd", options);
#endif
  REQUIRE(result.ok());
  CHECK(result->stdout_output.find(subdir.filename().string()) != std::string::npos);
}

TEST_CASE("Host exec times out") {
  LocalHost host(MakeTempDir("exec-timeout"));
  ExecOptions options;
  options.timeout_seconds = 1;
#ifdef _WIN32
  auto result = host.Exec("ping 127.0.0.1 -n 4 > nul", options);
#else
  auto result = host.Exec("sleep 3", options);
#endif
  REQUIRE(result.ok());
  CHECK(result->timed_out);
}

}  // namespace codeharness::host
