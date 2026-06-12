// Platform module tests
// Tests LocalPlatform (filesystem and process execution abstraction).

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "codeharness/platform/exec_result.h"
#include "codeharness/platform/local_platform.h"
#include "codeharness/platform/platform.h"
#include "codeharness/platform/stat_result.h"

namespace fs = std::filesystem;

namespace codeharness::platform {

namespace {

fs::path MakeTempDir() {
  auto dir = fs::temp_directory_path() / "codeharness_platform_test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

}  // namespace

TEST_CASE("Platform cwd returns initial value") {
  auto temp_dir = MakeTempDir();
  LocalPlatform platform(temp_dir);
  CHECK(platform.Cwd() == temp_dir);
}

TEST_CASE("Platform SetCwd canonicalizes path") {
  auto temp_dir = MakeTempDir();
  auto sub = temp_dir / "subdir";
  fs::create_directory(sub);

  LocalPlatform platform(temp_dir);
  platform.SetCwd(temp_dir / "subdir");
  CHECK(platform.Cwd() == fs::canonical(sub));
}

TEST_CASE("Platform Normpath resolves . and ..") {
  auto temp_dir = MakeTempDir();
  LocalPlatform platform(temp_dir);
  auto resolved = platform.Normpath(temp_dir / "." / "..");
  CHECK(resolved == fs::canonical(temp_dir).parent_path());
}

TEST_CASE("Platform Gethome returns non-empty") {
  LocalPlatform platform(fs::temp_directory_path());
  CHECK_FALSE(platform.Gethome().empty());
}

TEST_CASE("Platform Exists returns true for existing file") {
  auto temp_dir = MakeTempDir();
  auto file = temp_dir / "exists.txt";
  std::ofstream{file} << "data";

  LocalPlatform platform(temp_dir);
  CHECK(platform.Exists(file));
  CHECK_FALSE(platform.Exists(temp_dir / "nonexistent.txt"));
}

TEST_CASE("Platform IsDirectory and IsRegularFile") {
  auto temp_dir = MakeTempDir();
  auto file = temp_dir / "test.txt";
  std::ofstream{file} << "x";

  LocalPlatform platform(temp_dir);
  CHECK(platform.IsRegularFile(file));
  CHECK_FALSE(platform.IsDirectory(file));
  CHECK(platform.IsDirectory(temp_dir));
}

TEST_CASE("Platform Iterdir lists contents") {
  auto temp_dir = MakeTempDir();
  std::ofstream{temp_dir / "a.txt"} << "1";
  std::ofstream{temp_dir / "b.txt"} << "2";
  std::ofstream{temp_dir / "c.txt"} << "3";

  LocalPlatform platform(temp_dir);
  auto result = platform.Iterdir(temp_dir);
  REQUIRE(result.ok());
  CHECK(result->size() == 3);
}

TEST_CASE("Platform Iterdir fails for non-directory") {
  auto temp_dir = MakeTempDir();
  auto file = temp_dir / "notdir.txt";
  std::ofstream{file} << "x";

  LocalPlatform platform(temp_dir);
  auto result = platform.Iterdir(file);
  CHECK_FALSE(result.ok());
}

TEST_CASE("Platform ReadText reads contents") {
  auto temp_dir = MakeTempDir();
  auto file = temp_dir / "read.txt";
  std::ofstream{file} << "hello\nworld";

  LocalPlatform platform(temp_dir);
  auto result = platform.ReadText(file);
  REQUIRE(result.ok());
  CHECK(*result == "hello\nworld");
}

TEST_CASE("Platform ReadText fails for missing file") {
  LocalPlatform platform(MakeTempDir());
  auto result = platform.ReadText(fs::temp_directory_path() / "definitely_not_a_real_file_xyz.txt");
  CHECK_FALSE(result.ok());
}

TEST_CASE("Platform WriteText creates file") {
  auto temp_dir = MakeTempDir();
  auto file = temp_dir / "write.txt";

  LocalPlatform platform(temp_dir);
  auto status = platform.WriteText(file, "content");
  REQUIRE(status.ok());
  CHECK(fs::exists(file));
  auto read_back = platform.ReadText(file);
  REQUIRE(read_back.ok());
  CHECK(*read_back == "content");
}

TEST_CASE("Platform WriteText overwrites existing file") {
  auto temp_dir = MakeTempDir();
  auto file = temp_dir / "overwrite.txt";

  LocalPlatform platform(temp_dir);
  REQUIRE(platform.WriteText(file, "v1").ok());
  REQUIRE(platform.WriteText(file, "v2").ok());
  auto read_back = platform.ReadText(file);
  REQUIRE(read_back.ok());
  CHECK(*read_back == "v2");
}

TEST_CASE("Platform Mkdir creates single directory") {
  auto temp_dir = MakeTempDir();
  auto dir = temp_dir / "single";

  LocalPlatform platform(temp_dir);
  REQUIRE(platform.Mkdir(dir).ok());
  CHECK(fs::is_directory(dir));
}

TEST_CASE("Platform Mkdir creates nested directories") {
  auto temp_dir = MakeTempDir();
  auto dir = temp_dir / "a" / "b" / "c";

  LocalPlatform platform(temp_dir);
  REQUIRE(platform.Mkdir(dir).ok());
  CHECK(fs::is_directory(dir));
}

TEST_CASE("Platform Mkdir exist_ok defaults to true") {
  auto temp_dir = MakeTempDir();
  auto dir = temp_dir / "exists";
  fs::create_directory(dir);

  LocalPlatform platform(temp_dir);
  CHECK(platform.Mkdir(dir).ok());
}

TEST_CASE("Platform Exec runs command and captures output") {
  auto temp_dir = MakeTempDir();
  LocalPlatform platform(temp_dir);
  ExecOptions opts;
  opts.timeout_seconds = 30;
  auto result = platform.Exec("echo hello", opts);
  REQUIRE(result.ok());
  CHECK(result->exit_status == 0);
  CHECK(result->stdout_output.find("hello") != std::string::npos);
}

TEST_CASE("Platform Exec returns non-zero exit status") {
  auto temp_dir = MakeTempDir();
  LocalPlatform platform(temp_dir);
  ExecOptions opts;
  opts.timeout_seconds = 30;
#ifdef _WIN32
  auto result = platform.Exec("cmd.exe /c exit 7", opts);
#else
  auto result = platform.Exec("exit 7", opts);
#endif
  REQUIRE(result.ok());
  CHECK(result->exit_status != 0);
}

TEST_CASE("Platform Stat on file populates size") {
  auto temp_dir = MakeTempDir();
  auto file = temp_dir / "stat.txt";
  std::ofstream{file} << "12345";

  LocalPlatform platform(temp_dir);
  auto result = platform.Stat(file);
  REQUIRE(result.ok());
  CHECK(result->is_regular_file);
  CHECK(result->size == 5);
}

TEST_CASE("Platform Stat on directory") {
  auto temp_dir = MakeTempDir();
  LocalPlatform platform(temp_dir);
  auto result = platform.Stat(temp_dir);
  REQUIRE(result.ok());
  CHECK(result->is_directory);
  CHECK_FALSE(result->is_regular_file);
}

TEST_CASE("Platform Stat fails for missing file") {
  LocalPlatform platform(MakeTempDir());
  auto result = platform.Stat(platform.Cwd() / "definitely_not_a_real_file_xyz.txt");
  CHECK_FALSE(result.ok());
}

TEST_CASE("Platform Glob matches files") {
  auto temp_dir = MakeTempDir();
  std::ofstream{temp_dir / "test1.txt"} << "a";
  std::ofstream{temp_dir / "test2.txt"} << "b";
  std::ofstream{temp_dir / "other.md"} << "c";

  LocalPlatform platform(temp_dir);
  auto result = platform.Glob("test*.txt");
  REQUIRE(result.ok());
  CHECK(result->size() == 2);
}

}  // namespace codeharness::platform
