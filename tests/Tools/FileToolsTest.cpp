#include <doctest/doctest.h>

#include <nlohmann/json.hpp>
#include <string>

#include "absl/status/statusor.h"
#include "Engine/Tool.h"
#include "Tools/EditFile.h"
#include "Tools/ReadFile.h"
#include "Tools/ToolTestFixture.h"
#include "Tools/WriteFile.h"

namespace tools = codeharness::tools;
namespace engine = codeharness::engine;
using json = nlohmann::json;

static engine::ToolContext CtxFor(host::Host *h)
{
	return engine::ToolContext{.host = h};
}

TEST_CASE("ReadFileTool: returns content with line numbers")
{
	ToolFixture fx;
	fx.WriteFile("a.txt", "alpha\nbeta\ngamma\n");

	tools::ReadFileTool read;
	auto res = read.Execute(json{{"path", "a.txt"}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(res->content == "1\talpha\n2\tbeta\n3\tgamma\n");
}

TEST_CASE("ReadFileTool: line_offset and n_lines paging")
{
	ToolFixture fx;
	fx.WriteFile("a.txt", "one\ntwo\nthree\nfour\n");

	tools::ReadFileTool read;
	auto res = read.Execute(json{{"path", "a.txt"}, {"line_offset", 2}, {"n_lines", 2}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(res->content == "2\ttwo\n3\tthree\n");
}

TEST_CASE("ReadFileTool: negative line_offset reads from end")
{
	ToolFixture fx;
	fx.WriteFile("a.txt", "one\ntwo\nthree\n");

	tools::ReadFileTool read;
	auto res = read.Execute(json{{"path", "a.txt"}, {"line_offset", -1}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(res->content == "3\tthree\n");
}

TEST_CASE("ReadFileTool: missing file is an error")
{
	ToolFixture fx;
	tools::ReadFileTool read;
	auto res = read.Execute(json{{"path", "nope.txt"}}, CtxFor(&fx.host));
	CHECK_FALSE(res.ok());
}

TEST_CASE("ReadFileTool: binary file is rejected")
{
	ToolFixture fx;
	fx.WriteFile("bin.dat", std::string("a\0b", 3));

	tools::ReadFileTool read;
	auto res = read.Execute(json{{"path", "bin.dat"}}, CtxFor(&fx.host));
	CHECK_FALSE(res.ok());
}

TEST_CASE("ReadFileTool: resolve rejects missing path")
{
	tools::ReadFileTool read;
	auto res = read.ResolveExecution(json::object());
	CHECK_FALSE(res.ok());
}

TEST_CASE("WriteFileTool: overwrite writes bytes")
{
	ToolFixture fx;
	tools::WriteFileTool write;
	auto res = write.Execute(json{{"path", "out.txt"}, {"content", "hello"}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(fx.ReadFile("out.txt") == "hello");
	CHECK(res->content.find("5 byte") != std::string::npos);
}

TEST_CASE("WriteFileTool: append appends to existing file")
{
	ToolFixture fx;
	fx.WriteFile("out.txt", "hello ");

	tools::WriteFileTool write;
	auto res = write.Execute(json{{"path", "out.txt"}, {"content", "world"}, {"mode", "append"}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(fx.ReadFile("out.txt") == "hello world");
}

TEST_CASE("WriteFileTool: append creates file when absent")
{
	ToolFixture fx;
	tools::WriteFileTool write;
	auto res = write.Execute(json{{"path", "new.txt"}, {"content", "x"}, {"mode", "append"}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(fx.ReadFile("new.txt") == "x");
}

TEST_CASE("WriteFileTool: rejects bad mode")
{
	ToolFixture fx;
	tools::WriteFileTool write;
	auto res = write.Execute(json{{"path", "x.txt"}, {"content", ""}, {"mode", "bogus"}}, CtxFor(&fx.host));
	CHECK_FALSE(res.ok());
}

TEST_CASE("EditFileTool: replaces a single occurrence")
{
	ToolFixture fx;
	fx.WriteFile("a.txt", "alpha beta gamma\n");

	tools::EditFileTool edit;
	auto res =
		edit.Execute(json{{"file_path", "a.txt"}, {"old_string", "beta"}, {"new_string", "BETA"}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(fx.ReadFile("a.txt") == "alpha BETA gamma\n");
}

TEST_CASE("EditFileTool: replace_all replaces every occurrence")
{
	ToolFixture fx;
	fx.WriteFile("a.txt", "x x x\n");

	tools::EditFileTool edit;
	auto res = edit.Execute(json{{"file_path", "a.txt"}, {"old_string", "x"}, {"new_string", "y"}, {"replace_all", true}},
							CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(fx.ReadFile("a.txt") == "y y y\n");
}

TEST_CASE("EditFileTool: multiple matches without replace_all is an error")
{
	ToolFixture fx;
	fx.WriteFile("a.txt", "x x x\n");

	tools::EditFileTool edit;
	auto res = edit.Execute(json{{"file_path", "a.txt"}, {"old_string", "x"}, {"new_string", "y"}}, CtxFor(&fx.host));
	CHECK_FALSE(res.ok());
}

TEST_CASE("EditFileTool: no match is an error")
{
	ToolFixture fx;
	fx.WriteFile("a.txt", "alpha\n");

	tools::EditFileTool edit;
	auto res = edit.Execute(json{{"file_path", "a.txt"}, {"old_string", "zzz"}, {"new_string", "y"}}, CtxFor(&fx.host));
	CHECK_FALSE(res.ok());
}

TEST_CASE("EditFileTool: identical old/new is an error")
{
	ToolFixture fx;
	fx.WriteFile("a.txt", "alpha\n");

	tools::EditFileTool edit;
	auto res = edit.Execute(json{{"file_path", "a.txt"}, {"old_string", "a"}, {"new_string", "a"}}, CtxFor(&fx.host));
	CHECK_FALSE(res.ok());
}
