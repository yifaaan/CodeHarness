#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "host/local_host.h"

namespace host = codeharness::host;

TEST_CASE("Process exit code")
{
	host::LocalHost host;
	auto proc = host.Exec("exit 42");
	CHECK(proc.ok());
	auto code = (*proc)->Wait();
	CHECK(code.ok());
	CHECK_EQ(*code, 42);
}

TEST_CASE("Process pid valid")
{
	host::LocalHost host;
	auto proc = host.Exec("echo ok");
	CHECK(proc.ok());
	auto pid = (*proc)->Pid();
	CHECK(pid.ok());
	CHECK_GT(*pid, 0);
	CHECK((*proc)->Wait().ok());
}
