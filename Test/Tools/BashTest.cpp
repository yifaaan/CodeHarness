#include "Tools/Bash.h"

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>
#include <stop_token>
#include <string>

#include "absl/status/statusor.h"
#include "Engine/Tool.h"
#include "Tools/ToolTestFixture.h"

namespace tools = codeharness::tools;
namespace engine = codeharness::engine;
using json = nlohmann::json;

#ifdef _WIN32
const char *kSlowCmd = "ping -n 30 127.0.0.1 > nul";
#else
const char *kSlowCmd = "sleep 30";
#endif

TEST_CASE("BashTool: runs a command and captures output")
{
	ToolFixture fx;
	tools::BashTool bash;
	auto res = bash.Execute(json{{"command", "echo hello"}}, engine::ToolContext{.host = &fx.host});
	REQUIRE(res.ok());
	CHECK(res->content.find("hello") != std::string::npos);
	CHECK(res->content.find("[exit code: 0]") != std::string::npos);
	CHECK_FALSE(res->isError);
}

TEST_CASE("BashTool: nonzero exit is an error result")
{
	ToolFixture fx;
	tools::BashTool bash;
	auto res = bash.Execute(json{{"command", "exit 3"}}, engine::ToolContext{.host = &fx.host});
	REQUIRE(res.ok());
	CHECK(res->isError);
	CHECK(res->content.find("[exit code: 3]") != std::string::npos);
}

TEST_CASE("BashTool: timeout kills the process")
{
	ToolFixture fx;
	tools::BashTool bash;
	auto res = bash.Execute(json{{"command", kSlowCmd}, {"timeout_ms", 500}}, engine::ToolContext{.host = &fx.host});
	REQUIRE(res.ok());
	CHECK(res->isError);
	CHECK(res->content.find("timed out") != std::string::npos);
}

TEST_CASE("BashTool: cancellation via stopToken")
{
	ToolFixture fx;
	std::stop_source src;
	src.request_stop();

	tools::BashTool bash;
	engine::ToolContext ctx{.host = &fx.host, .stopToken = src.get_token()};
	auto res = bash.Execute(json{{"command", kSlowCmd}, {"timeout_ms", 60000}}, ctx);
	REQUIRE(res.ok());
	CHECK(res->isError);
	CHECK(res->content.find("cancelled") != std::string::npos);
}

TEST_CASE("BashTool: empty command is rejected")
{
	ToolFixture fx;
	tools::BashTool bash;
	auto res = bash.Execute(json{{"command", ""}}, engine::ToolContext{.host = &fx.host});
	CHECK_FALSE(res.ok());
}
