#include "Host/Environment.h"

#include <doctest/doctest.h>

#include <string>

#include "Host/LocalHost.h"

namespace host = codeharness::host;

TEST_CASE("detect_environment returns a valid shell path")
{
	auto env = host::DetectEnvironment();
	CHECK_FALSE(env.shellPath.empty());
	CHECK_FALSE(env.shellName.empty());
}

TEST_CASE("probe_shell finds an executable")
{
	auto shell = host::ProbeShell();
	CHECK_FALSE(shell.empty());
	std::error_code ec;
	bool exists = std::filesystem::exists(shell, ec);
	CHECK(exists);
}

TEST_CASE("environment knows the OS type")
{
	auto env = host::DetectEnvironment();
#ifdef _WIN32
	CHECK(env.isWindows);
#else
	CHECK_FALSE(env.isWindows);
#endif
}

TEST_CASE("environment has PATH directories")
{
	auto env = host::DetectEnvironment();
	CHECK_FALSE(env.pathDirs.empty());
}

TEST_CASE("shell name is one of the expected values")
{
	auto env = host::DetectEnvironment();
	bool validName = (env.shellName == "bash" || env.shellName == "sh" || env.shellName == "cmd");
	CHECK(validName);
}

TEST_CASE("LocalHost uses detected shell")
{
	host::LocalHost host;
	auto cwd = host.GetCwd();
	CHECK(cwd.ok());
	CHECK_FALSE(cwd->empty());
}
