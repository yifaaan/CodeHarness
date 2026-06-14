#include <doctest/doctest.h>

#include <nlohmann/json.hpp>
#include <string>

#include "Engine/Tool.h"
#include "Tools/Glob.h"
#include "Tools/Grep.h"
#include "Tools/ToolTestFixture.h"
#include "absl/status/statusor.h"

namespace tools = codeharness::tools;
namespace engine = codeharness::engine;
using json = nlohmann::json;

static engine::ToolContext CtxFor(host::Host* h)
{
	return engine::ToolContext{.host = h};
}

TEST_CASE("GlobTool: matches files by pattern")
{
	ToolFixture fx;
	fx.WriteFile("alpha.txt", "x");
	fx.WriteFile("beta.md", "x");
	fx.WriteFile("sub/gamma.txt", "x");

	tools::GlobTool glob;
	auto res = glob.Execute(json{{"pattern", "*.txt"}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(res->content.find("alpha.txt") != std::string::npos);
	CHECK(res->content.find("beta.md") == std::string::npos); // non-recursive, top-level only
}

TEST_CASE("GlobTool: recursive pattern descends into subdirectories")
{
	ToolFixture fx;
	fx.WriteFile("sub/gamma.txt", "x");

	tools::GlobTool glob;
	auto res = glob.Execute(json{{"pattern", "**/*.txt"}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(res->content.find("gamma.txt") != std::string::npos);
}

TEST_CASE("GlobTool: rejects overly broad patterns")
{
	ToolFixture fx;
	tools::GlobTool glob;
	auto res = glob.Execute(json{{"pattern", "**"}}, CtxFor(&fx.host));
	CHECK_FALSE(res.ok());
}

TEST_CASE("GrepTool: content mode shows matching lines")
{
	ToolFixture fx;
	fx.WriteFile("a.txt", "foo\nbar\nbaz\n");
	fx.WriteFile("b.txt", "foo\nqux\n");

	tools::GrepTool grep;
	auto res = grep.Execute(json{{"pattern", "foo"}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(res->content.find("foo") != std::string::npos);
	// Both files containing "foo" should appear.
	CHECK(res->content.find("a.txt") != std::string::npos);
	CHECK(res->content.find("b.txt") != std::string::npos);
}

TEST_CASE("GrepTool: files_with_matches lists only matching files")
{
	ToolFixture fx;
	fx.WriteFile("a.txt", "foo\nbar\n");
	fx.WriteFile("b.txt", "qux\n");

	tools::GrepTool grep;
	auto res = grep.Execute(json{{"pattern", "foo"}, {"output_mode", "files_with_matches"}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(res->content.find("a.txt") != std::string::npos);
	CHECK(res->content.find("b.txt") == std::string::npos);
}

TEST_CASE("GrepTool: count mode reports per-file match counts")
{
	ToolFixture fx;
	fx.WriteFile("a.txt", "foo\nfoo\nbar\n");

	tools::GrepTool grep;
	auto res = grep.Execute(json{{"pattern", "foo"}, {"output_mode", "count"}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(res->content.find(":2") != std::string::npos);
}

TEST_CASE("GrepTool: ignore_case matches case variants")
{
	ToolFixture fx;
	fx.WriteFile("a.txt", "Hello\n");

	tools::GrepTool grep;
	auto res = grep.Execute(json{{"pattern", "hello"}, {"ignore_case", true}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(res->content.find("Hello") != std::string::npos);
}

TEST_CASE("GrepTool: context lines are included")
{
	ToolFixture fx;
	fx.WriteFile("a.txt", "one\ntwo\nthree\nfour\nfive\n");

	tools::GrepTool grep;
	auto res = grep.Execute(json{{"pattern", "three"}, {"context", 1}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	// The match line and both context neighbors should be present.
	CHECK(res->content.find("two") != std::string::npos);
	CHECK(res->content.find("three") != std::string::npos);
	CHECK(res->content.find("four") != std::string::npos);
	CHECK(res->content.find("five") == std::string::npos); // outside context window
}

TEST_CASE("GrepTool: glob filter restricts searched files")
{
	ToolFixture fx;
	fx.WriteFile("a.cpp", "target\n");
	fx.WriteFile("b.md", "target\n");

	tools::GrepTool grep;
	auto res = grep.Execute(json{{"pattern", "target"}, {"glob", "*.cpp"}, {"output_mode", "files_with_matches"}},
							CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(res->content.find("a.cpp") != std::string::npos);
	CHECK(res->content.find("b.md") == std::string::npos);
}

TEST_CASE("GrepTool: invalid regex is rejected")
{
	ToolFixture fx;
	tools::GrepTool grep;
	auto res = grep.Execute(json{{"pattern", "[unterminated"}}, CtxFor(&fx.host));
	CHECK_FALSE(res.ok());
}

TEST_CASE("GrepTool: no matches returns a placeholder")
{
	ToolFixture fx;
	fx.WriteFile("a.txt", "nothing here\n");

	tools::GrepTool grep;
	auto res = grep.Execute(json{{"pattern", "zzz"}}, CtxFor(&fx.host));
	REQUIRE(res.ok());
	CHECK(res->content.find("no matches") != std::string::npos);
}
