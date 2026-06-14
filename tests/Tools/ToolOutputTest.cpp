#include "Tools/ToolOutput.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace tools = codeharness::tools;

TEST_CASE("SplitLines: empty input yields no lines")
{
	auto lines = tools::SplitLines("");
	CHECK(lines.empty());
}

TEST_CASE("SplitLines: splits on LF")
{
	auto lines = tools::SplitLines("a\nb\nc");
	REQUIRE(lines.size() == 3);
	CHECK(lines[0] == "a");
	CHECK(lines[1] == "b");
	CHECK(lines[2] == "c");
}

TEST_CASE("SplitLines: trailing newline is a terminator, not a blank line")
{
	auto lines = tools::SplitLines("a\nb\n");
	REQUIRE(lines.size() == 2);
	CHECK(lines[0] == "a");
	CHECK(lines[1] == "b");
}

TEST_CASE("SplitLines: strips trailing CR from CRLF")
{
	auto lines = tools::SplitLines("a\r\nb\r\n");
	REQUIRE(lines.size() == 2);
	CHECK(lines[0] == "a");
	CHECK(lines[1] == "b");
}

TEST_CASE("NumberLines: prefixes 1-based line numbers")
{
	std::vector<std::string> lines{"alpha", "beta"};
	CHECK(tools::NumberLines(lines) == "1\talpha\n2\tbeta\n");
}

TEST_CASE("NumberLines: respects custom start offset")
{
	std::vector<std::string> lines{"x"};
	CHECK(tools::NumberLines(lines, 5) == "5\tx\n");
}

TEST_CASE("TruncateOutput: short output passes through")
{
	CHECK(tools::TruncateOutput("hello\n", 100, 100) == "hello\n");
}

TEST_CASE("TruncateOutput: long single line is truncated")
{
	std::string longLine(300, 'a');
	auto out = tools::TruncateOutput(longLine, 100000, 100);
	CHECK(out.find("[line truncated]") != std::string::npos);
}

TEST_CASE("TruncateOutput: total output is capped")
{
	std::string big;
	for (int i = 0; i < 1000; ++i)
		big += "0123456789\n"; // 11 bytes * 1000
	auto out = tools::TruncateOutput(big, 500, 100000);
	CHECK(out.find("[output truncated at 500 chars]") != std::string::npos);
}
