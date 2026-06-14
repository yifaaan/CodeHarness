#include "host/local_host.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "host/host_path.h"

namespace host = codeharness::host;

struct LocalHostFixture
{
	host::LocalHost host;
	std::filesystem::path tmpDir;

	LocalHostFixture()
	{
		auto tmpBase = std::filesystem::temp_directory_path();
		tmpDir = tmpBase / ("codeharness_test_" + std::to_string(std::time(nullptr)));
		std::filesystem::create_directories(tmpDir);
		CHECK(host.Chdir(tmpDir.string()).ok());

		CreateFile("hello.txt", "Hello, World!\n");
		CreateFile("readme.md", "# Readme\n\nThis is a test.\n");
		CreateFile("script.sh", "#!/bin/bash\necho 'hello'\n");
		CreateFile("data.json", "{\"key\": \"value\"}\n");

		std::filesystem::create_directories(tmpDir / "subdir");
		CreateFile("subdir/alpha.txt", "alpha content\n");
		CreateFile("subdir/beta.txt", "beta content\n");
		std::filesystem::create_directories(tmpDir / "subdir" / "nested");
		CreateFile("subdir/nested/deep.txt", "deep content\n");

		std::filesystem::create_directories(tmpDir / "empty_dir");
		CreateFile("numbers.txt", "one\ntwo\nthree\nfour\nfive\n");
	}

	~LocalHostFixture()
	{
		std::error_code ec;
		std::filesystem::remove_all(tmpDir, ec);
	}

	void CreateFile(const std::string& relPath, const std::string& content)
	{
		auto fullPath = tmpDir / relPath;
		std::ofstream file(fullPath, std::ios::binary);
		file << content;
	}
};

#define CHECK_OK(expr)   \
	do                   \
	{                    \
		auto S = (expr); \
		CHECK(S.ok());   \
	}                    \
	while (0)
#define CHECK_VALUE(expr, expected)    \
	do                                 \
	{                                  \
		auto V = (expr);               \
		CHECK(V.ok());                 \
		CHECK_EQ(V.value(), expected); \
	}                                  \
	while (0)

TEST_CASE("LocalHost::path_class")
{
	LocalHostFixture f;
#ifdef _WIN32
	CHECK_EQ(f.host.PathClass(), "win32");
#else
	CHECK_EQ(f.host.PathClass(), "posix");
#endif
}

TEST_CASE("LocalHost::normpath")
{
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

TEST_CASE("LocalHost::gethome")
{
	LocalHostFixture f;
	auto home = f.host.GetHome();
	CHECK(home.ok());
	CHECK_FALSE(home->empty());
	CHECK_NE(*home, ".");
}

TEST_CASE("LocalHost::getcwd")
{
	LocalHostFixture f;
	CHECK_VALUE(f.host.GetCwd(), f.tmpDir.string());
}

TEST_CASE("LocalHost::chdir")
{
	LocalHostFixture f;
	CHECK_OK(f.host.Chdir("subdir"));
	CHECK_VALUE(f.host.GetCwd(), (f.tmpDir / "subdir").string());
}

TEST_CASE("LocalHost::stat - regular file")
{
	LocalHostFixture f;
	auto s = f.host.Stat("hello.txt");
	CHECK(s.ok());
	CHECK_GT(s->stSize, 0);
	CHECK((s->stMode & 0170000) == 0100000);
}

TEST_CASE("LocalHost::stat - directory")
{
	LocalHostFixture f;
	auto s = f.host.Stat("subdir");
	CHECK(s.ok());
	CHECK((s->stMode & 0170000) == 0040000);
}

TEST_CASE("LocalHost::stat - nonexistent file")
{
	LocalHostFixture f;
	auto s = f.host.Stat("nonexistent.txt");
	CHECK_FALSE(s.ok());
	CHECK(absl::IsNotFound(s.status()));
}

TEST_CASE("LocalHost::iterdir")
{
	LocalHostFixture f;
	auto entries = f.host.Iterdir(".");
	CHECK(entries.ok());
	CHECK_GE(entries->size(), 5);
	CHECK(std::find(entries->begin(), entries->end(), "hello.txt") != entries->end());
	CHECK(std::find(entries->begin(), entries->end(), "subdir") != entries->end());
}

TEST_CASE("LocalHost::iterdir - subdir")
{
	LocalHostFixture f;
	auto entries = f.host.Iterdir("subdir");
	CHECK(entries.ok());
	CHECK_EQ(entries->size(), 3);
	CHECK(std::find(entries->begin(), entries->end(), "alpha.txt") != entries->end());
	CHECK(std::find(entries->begin(), entries->end(), "beta.txt") != entries->end());
	CHECK(std::find(entries->begin(), entries->end(), "nested") != entries->end());
}

TEST_CASE("LocalHost::read_text")
{
	LocalHostFixture f;
	auto content = f.host.ReadText("hello.txt");
	CHECK(content.ok());
	CHECK_EQ(*content, "Hello, World!\n");
}

TEST_CASE("LocalHost::read_text - nonexistent file")
{
	LocalHostFixture f;
	auto content = f.host.ReadText("no_such_file.txt");
	CHECK_FALSE(content.ok());
	CHECK(absl::IsNotFound(content.status()));
}

TEST_CASE("LocalHost::read_lines - all")
{
	LocalHostFixture f;
	auto lines = f.host.ReadLines("numbers.txt");
	CHECK(lines.ok());
	CHECK_EQ(lines->size(), 5);
	CHECK_EQ((*lines)[0], "one");
	CHECK_EQ((*lines)[3], "four");
}

TEST_CASE("LocalHost::read_lines - first N")
{
	LocalHostFixture f;
	auto lines = f.host.ReadLines("numbers.txt", 3);
	CHECK(lines.ok());
	CHECK_EQ(lines->size(), 3);
	CHECK_EQ((*lines)[0], "one");
	CHECK_EQ((*lines)[2], "three");
}

TEST_CASE("LocalHost::write_text + read_text roundtrip")
{
	LocalHostFixture f;
	CHECK_OK(f.host.WriteText("new_file.txt", "fresh content\nsecond line\n"));
	auto content = f.host.ReadText("new_file.txt");
	CHECK(content.ok());
	CHECK_EQ(*content, "fresh content\nsecond line\n");
}

TEST_CASE("LocalHost::read_bytes")
{
	LocalHostFixture f;
	auto bytes = f.host.ReadBytes("hello.txt");
	CHECK(bytes.ok());
	std::string content(bytes->begin(), bytes->end());
	CHECK_EQ(content, "Hello, World!\n");
}

TEST_CASE("LocalHost::write_bytes + read_bytes roundtrip")
{
	LocalHostFixture f;
	std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
	CHECK_OK(f.host.WriteBytes("binary.dat", data));
	auto readBack = f.host.ReadBytes("binary.dat");
	CHECK(readBack.ok());
	CHECK_EQ(readBack->size(), 4);
	CHECK_EQ((*readBack)[0], 0xDE);
	CHECK_EQ((*readBack)[3], 0xEF);
}

TEST_CASE("LocalHost::mkdir - new directory")
{
	LocalHostFixture f;
	CHECK_OK(f.host.Mkdir("new_dir"));
	auto s = f.host.Stat("new_dir");
	CHECK(s.ok());
	CHECK((s->stMode & 0170000) == 0040000);
}

TEST_CASE("LocalHost::mkdir - exist_ok")
{
	LocalHostFixture f;
	CHECK_OK(f.host.Mkdir("subdir"));
}

TEST_CASE("LocalHost::mkdir - exist_ok = false")
{
	LocalHostFixture f;
	host::MkdirOptions opts;
	opts.existOk = false;
	auto result = f.host.Mkdir("subdir", opts);
	CHECK_FALSE(result.ok());
	CHECK(absl::IsAlreadyExists(result));
}

TEST_CASE("LocalHost::mkdir - recursive")
{
	LocalHostFixture f;
	host::MkdirOptions opts;
	opts.recursive = true;
	CHECK_OK(f.host.Mkdir("a/b/c", opts));
	CHECK(f.host.Stat("a/b/c").ok());
}

TEST_CASE("LocalHost::glob - all txt files")
{
	LocalHostFixture f;
	auto results = f.host.Glob("*.txt");
	CHECK(results.ok());
	CHECK_GE(results->size(), 2);
	bool hasHello = false;
	bool hasNumbers = false;
	for (const auto& r : *results)
	{
		if (r.find("hello.txt") != std::string::npos)
			hasHello = true;
		if (r.find("numbers.txt") != std::string::npos)
			hasNumbers = true;
	}
	CHECK(hasHello);
	CHECK(hasNumbers);
}

TEST_CASE("LocalHost::glob - single file")
{
	LocalHostFixture f;
	auto results = f.host.Glob("hello.txt");
	CHECK(results.ok());
	REQUIRE_EQ(results->size(), 1);
}

TEST_CASE("LocalHost::glob - no match")
{
	LocalHostFixture f;
	auto results = f.host.Glob("*.xyz");
	CHECK(results.ok());
	CHECK(results->empty());
}

TEST_CASE("LocalHost::glob - question mark wildcard")
{
	LocalHostFixture f;
	auto results = f.host.Glob("hello.?xt");
	CHECK(results.ok());
	REQUIRE_EQ(results->size(), 1);
}

TEST_CASE("LocalHost::exec - exit code")
{
	LocalHostFixture f;
	auto proc = f.host.Exec("exit 42");
	CHECK(proc.ok());
	auto exitCode = (*proc)->Wait();
	CHECK(exitCode.ok());
	CHECK_EQ(*exitCode, 42);
}

TEST_CASE("HostPath - exists")
{
	LocalHostFixture f;
	host::HostPath hp(f.tmpDir / "hello.txt");
	auto exists = hp.Exists(&f.host);
	CHECK(exists.ok());
	CHECK(*exists);
}

TEST_CASE("HostPath - not exists")
{
	LocalHostFixture f;
	host::HostPath hp(f.tmpDir / "does_not_exist.txt");
	auto exists = hp.Exists(&f.host);
	CHECK(exists.ok());
	CHECK_FALSE(*exists);
}

TEST_CASE("HostPath - is_file / is_dir")
{
	LocalHostFixture f;
	host::HostPath filePath(f.tmpDir / "hello.txt");
	host::HostPath dirPath(f.tmpDir / "subdir");
	auto isF = filePath.IsFile(&f.host);
	auto isD = dirPath.IsDir(&f.host);
	CHECK(isF.ok());
	CHECK(isD.ok());
	CHECK(*isF);
	CHECK(*isD);
	auto notD = filePath.IsDir(&f.host);
	auto notF = dirPath.IsFile(&f.host);
	CHECK(notD.ok());
	CHECK(notF.ok());
	CHECK_FALSE(*notD);
	CHECK_FALSE(*notF);
}

TEST_CASE("HostPath - name / parent")
{
	host::HostPath hp("/foo/bar/baz.txt");
	CHECK_EQ(hp.Name(), "baz.txt");
	CHECK_EQ(hp.Parent().String(), "/foo/bar");
}
