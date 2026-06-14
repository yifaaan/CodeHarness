#include "llm/sse_parser.h"

#include <doctest/doctest.h>

#include <optional>
#include <string>

namespace llm = codeharness::llm;

TEST_CASE("SseParser: single event with LF")
{
	llm::SseParser parser;
	parser.Feed("data: {\"key\":\"value\"}\n\n");

	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "{\"key\":\"value\"}");
	CHECK_FALSE(parser.NextEvent().has_value());
}

TEST_CASE("SseParser: single event with CRLF")
{
	llm::SseParser parser;
	parser.Feed("data: hello\r\n\r\n");

	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "hello");
}

TEST_CASE("SseParser: single event with CR only")
{
	llm::SseParser parser;
	parser.Feed("data: hello\r\r");

	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "hello");
}

TEST_CASE("SseParser: multiple events in one feed")
{
	llm::SseParser parser;
	parser.Feed("data: first\n\ndata: second\n\ndata: third\n\n");

	auto e1 = parser.NextEvent();
	REQUIRE(e1.has_value());
	CHECK(*e1 == "first");

	auto e2 = parser.NextEvent();
	REQUIRE(e2.has_value());
	CHECK(*e2 == "second");

	auto e3 = parser.NextEvent();
	REQUIRE(e3.has_value());
	CHECK(*e3 == "third");

	CHECK_FALSE(parser.NextEvent().has_value());
}

TEST_CASE("SseParser: partial input across feeds")
{
	llm::SseParser parser;

	parser.Feed("data: hel");
	CHECK_FALSE(parser.NextEvent().has_value());

	parser.Feed("lo\n");
	CHECK_FALSE(parser.NextEvent().has_value());

	parser.Feed("\n");
	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "hello");
}

TEST_CASE("SseParser: partial line split mid-data")
{
	llm::SseParser parser;

	parser.Feed("data: {\"id\":");
	CHECK_FALSE(parser.NextEvent().has_value());

	parser.Feed("\"abc\"}\n\n");
	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "{\"id\":\"abc\"}");
}

TEST_CASE("SseParser: DONE marker sets done flag")
{
	llm::SseParser parser;
	parser.Feed("data: {\"content\":\"hi\"}\n\ndata: [DONE]\n\n");

	auto e1 = parser.NextEvent();
	REQUIRE(e1.has_value());
	CHECK(*e1 == "{\"content\":\"hi\"}");

	CHECK_FALSE(parser.NextEvent().has_value());
	CHECK(parser.Done());
}

TEST_CASE("SseParser: Done returns nullopt after [DONE]")
{
	llm::SseParser parser;
	parser.Feed("data: [DONE]\n\n");

	CHECK_FALSE(parser.NextEvent().has_value());
	CHECK(parser.Done());
}

TEST_CASE("SseParser: multi-line data joined with newline")
{
	llm::SseParser parser;
	parser.Feed("data: line1\ndata: line2\ndata: line3\n\n");

	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "line1\nline2\nline3");
}

TEST_CASE("SseParser: comment lines ignored")
{
	llm::SseParser parser;
	parser.Feed(": this is a comment\ndata: payload\n\n");

	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "payload");
}

TEST_CASE("SseParser: non-data fields ignored")
{
	llm::SseParser parser;
	parser.Feed("event: message\ndata: payload\nid: 42\nretry: 1000\n\n");

	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "payload");
}

TEST_CASE("SseParser: data without space after colon")
{
	llm::SseParser parser;
	parser.Feed("data:{\"key\":\"val\"}\n\n");

	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "{\"key\":\"val\"}");
}

TEST_CASE("SseParser: data with multiple spaces preserves extras")
{
	llm::SseParser parser;
	parser.Feed("data:  two spaces\n\n");

	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == " two spaces");
}

TEST_CASE("SseParser: empty data line")
{
	llm::SseParser parser;
	parser.Feed("data:\n\n");

	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "");
}

TEST_CASE("SseParser: empty lines without data are skipped")
{
	llm::SseParser parser;
	parser.Feed("\n\ndata: payload\n\n");

	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "payload");
}

TEST_CASE("SseParser: realistic OpenAI SSE stream")
{
	llm::SseParser parser;
	std::string stream =
		"data: {\"id\":\"chatcmpl-1\",\"choices\":[{\"delta\":{\"role\":\"assistant\"}}]}\n\n"
		"data: {\"id\":\"chatcmpl-1\",\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n\n"
		"data: {\"id\":\"chatcmpl-1\",\"choices\":[{\"delta\":{\"content\":\" world\"}}]}\n\n"
		"data: {\"id\":\"chatcmpl-1\",\"choices\":[{\"finish_reason\":\"stop\"}]}\n\n"
		"data: [DONE]\n\n";

	parser.Feed(stream);

	int eventCount = 0;
	while (auto event = parser.NextEvent())
	{
		CHECK_FALSE(*event == "[DONE]");
		++eventCount;
	}
	CHECK(eventCount == 4);
	CHECK(parser.Done());
}

TEST_CASE("SseParser: Reset clears state")
{
	llm::SseParser parser;
	parser.Feed("data: [DONE]\n\n");
	CHECK_FALSE(parser.NextEvent().has_value());
	CHECK(parser.Done());

	parser.Reset();
	CHECK_FALSE(parser.Done());

	parser.Feed("data: fresh\n\n");
	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "fresh");
}

TEST_CASE("SseParser: no trailing newline does not dispatch")
{
	llm::SseParser parser;
	parser.Feed("data: incomplete");
	CHECK_FALSE(parser.NextEvent().has_value());

	parser.Feed("\n\n");
	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "incomplete");
}

TEST_CASE("SseParser: mixed line endings within one event")
{
	llm::SseParser parser;
	parser.Feed("data: first\r\ndata: second\r\ndata: third\r\n\r\n");

	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "first\nsecond\nthird");
}

TEST_CASE("SseParser: bare CR as event boundary")
{
	llm::SseParser parser;
	parser.Feed("data: hello\r\r");

	auto event = parser.NextEvent();
	REQUIRE(event.has_value());
	CHECK(*event == "hello");
}
