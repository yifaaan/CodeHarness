#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "host/local_host.h"
#include "host/host_path.h"

namespace host = codeharness::host;

struct LocalHostFixture {
  host::LocalHost host;
  std::filesystem::path tmp_dir;

  LocalHostFixture() {
    auto tmp_base = std::filesystem::temp_directory_path();
    tmp_dir = tmp_base / ("codeharness_test_" + std::to_string(std::time(nullptr)));
    std::filesystem::create_directories(tmp_dir);
    CHECK(host.Chdir(tmp_dir.string()).ok());

    create_file("hello.txt", "Hello, World!\n");
    create_file("readme.md", "# Readme\n\nThis is a test.\n");
    create_file("script.sh", "#!/bin/bash\necho 'hello'\n");
    create_file("data.json", "{\"key\": \"value\"}\n");

    std::filesystem::create_directories(tmp_dir / "subdir");
    create_file("subdir/alpha.txt", "alpha content\n");
    create_file("subdir/beta.txt", "beta content\n");
    std::filesystem::create_directories(tmp_dir / "subdir" / "nested");
    create_file("subdir/nested/deep.txt", "deep content\n");

    std::filesystem::create_directories(tmp_dir / "empty_dir");
    create_file("numbers.txt", "one\ntwo\nthree\nfour\nfive\n");
  }

  ~LocalHostFixture() {
    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);
  }

  void create_file(const std::string& rel_path, const std::string& content) {
    auto full_path = tmp_dir / rel_path;
    std::ofstream file(full_path, std::ios::binary);
    file << content;
  }
};

#define CHECK_OK(expr) do { auto _s = (expr); CHECK(_s.ok()); } while(0)
#define CHECK_VALUE(expr, expected) do { auto _v = (expr); CHECK(_v.ok()); CHECK_EQ(_v.value(), expected); } while(0)

TEST_CASE("LocalHost::path_class") {
  LocalHostFixture f;
#ifdef _WIN32
  CHECK_EQ(f.host.PathClass(), "win32");
#else
  CHECK_EQ(f.host.PathClass(), "posix");
#endif
}

TEST_CASE("LocalHost::normpath") {
  LocalHostFixture f;
  auto normalized = f.host.Normpath("foo/bar");
  CHECK(normalized.ok());
  bool ok = (normalized.value() == "foo/bar" || normalized.value() == "foo\\bar");
  CHECK(ok);
  CHECK_VALUE(f.host.Normpath("foo/../bar"), "bar");
  auto normalized2 = f.host.Normpath("foo/./bar");
  CHECK(normalized2.ok());
  bool ok2 = (normalized2.value() == "foo/bar" || normalized2.value() == "foo\\bar");
  CHECK(ok2);
}

TEST_CASE("LocalHost::gethome") {
  LocalHostFixture f;
  auto home = f.host.GetHome();
  CHECK(home.ok());
  CHECK_FALSE(home->empty());
  CHECK_NE(*home, ".");
}

TEST_CASE("LocalHost::getcwd") {
  LocalHostFixture f;
  CHECK_VALUE(f.host.GetCwd(), f.tmp_dir.string());
}

TEST_CASE("LocalHost::chdir") {
  LocalHostFixture f;
  CHECK_OK(f.host.Chdir("subdir"));
  CHECK_VALUE(f.host.GetCwd(), (f.tmp_dir / "subdir").string());
}

TEST_CASE("LocalHost::stat - regular file") {
  LocalHostFixture f;
  auto s = f.host.Stat("hello.txt");
  CHECK(s.ok());
  CHECK_GT(s->st_size, 0);
  CHECK((s->st_mode & 0170000) == 0100000);
}

TEST_CASE("LocalHost::stat - directory") {
  LocalHostFixture f;
  auto s = f.host.Stat("subdir");
  CHECK(s.ok());
  CHECK((s->st_mode & 0170000) == 0040000);
}

TEST_CASE("LocalHost::stat - nonexistent file") {
  LocalHostFixture f;
  auto s = f.host.Stat("nonexistent.txt");
  CHECK_FALSE(s.ok());
  CHECK(absl::IsNotFound(s.status()));
}

TEST_CASE("LocalHost::iterdir") {
  LocalHostFixture f;
  auto entries = f.host.Iterdir(".");
  CHECK(entries.ok());
  CHECK_GE(entries->size(), 5);
  CHECK(std::find(entries->begin(), entries->end(), "hello.txt") != entries->end());
  CHECK(std::find(entries->begin(), entries->end(), "subdir") != entries->end());
}

TEST_CASE("LocalHost::iterdir - subdir") {
  LocalHostFixture f;
  auto entries = f.host.Iterdir("subdir");
  CHECK(entries.ok());
  CHECK_EQ(entries->size(), 3);
  CHECK(std::find(entries->begin(), entries->end(), "alpha.txt") != entries->end());
  CHECK(std::find(entries->begin(), entries->end(), "beta.txt") != entries->end());
  CHECK(std::find(entries->begin(), entries->end(), "nested") != entries->end());
}

TEST_CASE("LocalHost::read_text") {
  LocalHostFixture f;
  auto content = f.host.ReadText("hello.txt");
  CHECK(content.ok());
  CHECK_EQ(*content, "Hello, World!\n");
}

TEST_CASE("LocalHost::read_text - nonexistent file") {
  LocalHostFixture f;
  auto content = f.host.ReadText("no_such_file.txt");
  CHECK_FALSE(content.ok());
  CHECK(absl::IsNotFound(content.status()));
}

TEST_CASE("LocalHost::read_lines - all") {
  LocalHostFixture f;
  auto lines = f.host.ReadLines("numbers.txt");
  CHECK(lines.ok());
  CHECK_EQ(lines->size(), 5);
  CHECK_EQ((*lines)[0], "one");
  CHECK_EQ((*lines)[3], "four");
}

TEST_CASE("LocalHost::read_lines - first N") {
  LocalHostFixture f;
  auto lines = f.host.ReadLines("numbers.txt", 3);
  CHECK(lines.ok());
  CHECK_EQ(lines->size(), 3);
  CHECK_EQ((*lines)[0], "one");
  CHECK_EQ((*lines)[2], "three");
}

TEST_CASE("LocalHost::write_text + read_text roundtrip") {
  LocalHostFixture f;
  CHECK_OK(f.host.WriteText("new_file.txt", "fresh content\nsecond line\n"));
  auto content = f.host.ReadText("new_file.txt");
  CHECK(content.ok());
  CHECK_EQ(*content, "fresh content\nsecond line\n");
}

TEST_CASE("LocalHost::read_bytes") {
  LocalHostFixture f;
  auto bytes = f.host.ReadBytes("hello.txt");
  CHECK(bytes.ok());
  std::string content(bytes->begin(), bytes->end());
  CHECK_EQ(content, "Hello, World!\n");
}

TEST_CASE("LocalHost::write_bytes + read_bytes roundtrip") {
  LocalHostFixture f;
  std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
  CHECK_OK(f.host.WriteBytes("binary.dat", data));
  auto read_back = f.host.ReadBytes("binary.dat");
  CHECK(read_back.ok());
  CHECK_EQ(read_back->size(), 4);
  CHECK_EQ((*read_back)[0], 0xDE);
  CHECK_EQ((*read_back)[3], 0xEF);
}

TEST_CASE("LocalHost::mkdir - new directory") {
  LocalHostFixture f;
  CHECK_OK(f.host.Mkdir("new_dir"));
  auto s = f.host.Stat("new_dir");
  CHECK(s.ok());
  CHECK((s->st_mode & 0170000) == 0040000);
}

TEST_CASE("LocalHost::mkdir - exist_ok") {
  LocalHostFixture f;
  CHECK_OK(f.host.Mkdir("subdir"));
}

TEST_CASE("LocalHost::mkdir - exist_ok = false") {
  LocalHostFixture f;
  host::MkdirOptions opts;
  opts.exist_ok = false;
  auto result = f.host.Mkdir("subdir", opts);
  CHECK_FALSE(result.ok());
  CHECK(absl::IsAlreadyExists(result));
}

TEST_CASE("LocalHost::mkdir - recursive") {
  LocalHostFixture f;
  host::MkdirOptions opts;
  opts.recursive = true;
  CHECK_OK(f.host.Mkdir("a/b/c", opts));
  CHECK(f.host.Stat("a/b/c").ok());
}

TEST_CASE("LocalHost::glob - all txt files") {
  LocalHostFixture f;
  auto results = f.host.Glob("*.txt");
  CHECK(results.ok());
  CHECK_GE(results->size(), 2);
  bool has_hello = false;
  bool has_numbers = false;
  for (const auto& r : *results) {
    if (r.find("hello.txt") != std::string::npos) has_hello = true;
    if (r.find("numbers.txt") != std::string::npos) has_numbers = true;
  }
  CHECK(has_hello);
  CHECK(has_numbers);
}

TEST_CASE("LocalHost::glob - single file") {
  LocalHostFixture f;
  auto results = f.host.Glob("hello.txt");
  CHECK(results.ok());
  REQUIRE_EQ(results->size(), 1);
}

TEST_CASE("LocalHost::glob - no match") {
  LocalHostFixture f;
  auto results = f.host.Glob("*.xyz");
  CHECK(results.ok());
  CHECK(results->empty());
}

TEST_CASE("LocalHost::glob - question mark wildcard") {
  LocalHostFixture f;
  auto results = f.host.Glob("hello.?xt");
  CHECK(results.ok());
  REQUIRE_EQ(results->size(), 1);
}

TEST_CASE("LocalHost::exec - exit code") {
  LocalHostFixture f;
  auto proc = f.host.Exec("exit 42");
  CHECK(proc.ok());
  auto exit_code = (*proc)->Wait();
  CHECK(exit_code.ok());
  CHECK_EQ(*exit_code, 42);
}

TEST_CASE("HostPath - exists") {
  LocalHostFixture f;
  host::HostPath hp(f.tmp_dir / "hello.txt");
  auto exists = hp.Exists(&f.host);
  CHECK(exists.ok());
  CHECK(*exists);
}

TEST_CASE("HostPath - not exists") {
  LocalHostFixture f;
  host::HostPath hp(f.tmp_dir / "does_not_exist.txt");
  auto exists = hp.Exists(&f.host);
  CHECK(exists.ok());
  CHECK_FALSE(*exists);
}

TEST_CASE("HostPath - is_file / is_dir") {
  LocalHostFixture f;
  host::HostPath file_path(f.tmp_dir / "hello.txt");
  host::HostPath dir_path(f.tmp_dir / "subdir");
  auto is_f = file_path.IsFile(&f.host);
  auto is_d = dir_path.IsDir(&f.host);
  CHECK(is_f.ok());
  CHECK(is_d.ok());
  CHECK(*is_f);
  CHECK(*is_d);
  auto not_d = file_path.IsDir(&f.host);
  auto not_f = dir_path.IsFile(&f.host);
  CHECK(not_d.ok());
  CHECK(not_f.ok());
  CHECK_FALSE(*not_d);
  CHECK_FALSE(*not_f);
}

TEST_CASE("HostPath - name / parent") {
  host::HostPath hp("/foo/bar/baz.txt");
  CHECK_EQ(hp.Name(), "baz.txt");
  CHECK_EQ(hp.Parent().String(), "/foo/bar");
}