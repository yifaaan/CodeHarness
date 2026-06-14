#include "Records/AgentRecords.h"
#include "Records/FilePersistence.h"
#include "Records/RecordJson.h"
#include "Records/RecordTypes.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <doctest/doctest.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Engine/LoopTypes.h"
#include "Host/LocalHost.h"
#include "Llm/Types.h"

namespace host = codeharness::host;
namespace llm = codeharness::llm;
namespace engine = codeharness::engine;
namespace records = codeharness::records;

namespace
{

	struct TmpDirFixture
	{
		host::LocalHost host;
		std::filesystem::path tmpDir;
		std::string wirePath;

		TmpDirFixture()
		{
			auto tmpBase = std::filesystem::temp_directory_path();
			tmpDir = tmpBase / ("codeharness_records_test_" + std::to_string(std::time(nullptr)));
			std::filesystem::create_directories(tmpDir);
			CHECK(host.Chdir(tmpDir.string()).ok());
			wirePath = (tmpDir / "wire.jsonl").string();
		}

		~TmpDirFixture()
		{
			std::error_code ec;
			std::filesystem::remove_all(tmpDir, ec);
		}
	};

	llm::Message MakeUserMessage(std::string text)
	{
		llm::Message m;
		m.role = llm::Role::User;
		m.content.push_back(llm::TextPart{std::move(text)});
		return m;
	}

} // namespace

#define CHECK_OK(expr)             \
	do                             \
	{                              \
		auto _s = (expr);          \
		CHECK(_s.ok());            \
		if (!_s.ok())              \
			MESSAGE(_s.message()); \
	} while (0)

TEST_CASE("Records: WireRecord round-trip for all 4 kinds")
{
	records::WireRecord w;
	w.meta.ts = 1718190000000;
	w.meta.protocol = "1.0";

	SUBCASE("turn.prompt")
	{
		records::TurnPromptRecord r;
		r.turnId = "turn_42";
		r.input.push_back(llm::TextPart{"hello"});
		r.origin = 0;
		w.record = std::move(r);

		auto json = records::WireRecordToJson(w);
		auto line = json.dump();
		auto parsed = records::ParseWireRecord(line);
		CHECK(parsed.ok());
		CHECK(parsed->meta.ts == 1718190000000);
		CHECK(parsed->meta.protocol == "1.0");
		auto* p = std::get_if<records::TurnPromptRecord>(&parsed->record);
		REQUIRE(p != nullptr);
		CHECK_EQ(p->turnId, "turn_42");
		REQUIRE_EQ(p->input.size(), 1u);
		auto* text = std::get_if<llm::TextPart>(&p->input[0]);
		REQUIRE(text != nullptr);
		CHECK_EQ(text->text, "hello");
		CHECK_EQ(p->origin, 0);
	}

	SUBCASE("turn.cancel")
	{
		records::TurnCancelRecord r;
		r.turnId = "turn_7";
		w.record = std::move(r);

		auto line = records::WireRecordToJson(w).dump();
		auto parsed = records::ParseWireRecord(line);
		CHECK(parsed.ok());
		auto* p = std::get_if<records::TurnCancelRecord>(&parsed->record);
		REQUIRE(p != nullptr);
		CHECK_EQ(p->turnId, "turn_7");
	}

	SUBCASE("context.append_message")
	{
		records::ContextAppendMessageRecord r;
		r.message = MakeUserMessage("hi there");
		w.record = std::move(r);

		auto line = records::WireRecordToJson(w).dump();
		auto parsed = records::ParseWireRecord(line);
		CHECK(parsed.ok());
		auto* p = std::get_if<records::ContextAppendMessageRecord>(&parsed->record);
		REQUIRE(p != nullptr);
		CHECK(p->message.role == llm::Role::User);
		REQUIRE_EQ(p->message.content.size(), 1u);
		auto* text = std::get_if<llm::TextPart>(&p->message.content[0]);
		REQUIRE(text != nullptr);
		CHECK_EQ(text->text, "hi there");
	}

	SUBCASE("context.append_loop_event - StepStarted")
	{
		records::ContextAppendLoopEventRecord r;
		r.event = engine::StepStartedEvent{3};
		w.record = std::move(r);

		auto line = records::WireRecordToJson(w).dump();
		auto parsed = records::ParseWireRecord(line);
		CHECK(parsed.ok());
		auto* p = std::get_if<records::ContextAppendLoopEventRecord>(&parsed->record);
		REQUIRE(p != nullptr);
		auto* e = std::get_if<engine::StepStartedEvent>(&p->event);
		REQUIRE(e != nullptr);
		CHECK_EQ(e->step, 3);
	}

	SUBCASE("context.append_loop_event - ToolResult with isError")
	{
		records::ContextAppendLoopEventRecord r;
		engine::ToolResultEvent tr;
		tr.id = "call_1";
		tr.name = "Bash";
		tr.result.content = "boom";
		tr.result.isError = true;
		r.event = std::move(tr);
		w.record = std::move(r);

		auto line = records::WireRecordToJson(w).dump();
		auto parsed = records::ParseWireRecord(line);
		CHECK(parsed.ok());
		auto* p = std::get_if<records::ContextAppendLoopEventRecord>(&parsed->record);
		REQUIRE(p != nullptr);
		auto* e = std::get_if<engine::ToolResultEvent>(&p->event);
		REQUIRE(e != nullptr);
		CHECK_EQ(e->id, "call_1");
		CHECK_EQ(e->name, "Bash");
		CHECK(e->result.isError);
		CHECK_EQ(e->result.content, "boom");
	}
}

TEST_CASE("Records: unknown record type returns InvalidArgument")
{
	auto status = records::ParseWireRecord(R"({"type":"future.unknown","ts":1})");
	CHECK_FALSE(status.ok());
	CHECK(absl::IsInvalidArgument(status.status()));
}

TEST_CASE("Records: malformed JSON returns InvalidArgument")
{
	auto status = records::ParseWireRecord(R"(not json at all)");
	CHECK_FALSE(status.ok());
	CHECK(absl::IsInvalidArgument(status.status()));
}

TEST_CASE("FilePersistence: append preserves insertion order")
{
	TmpDirFixture f;

	records::FilePersistence fp(&f.host, f.wirePath);
	records::WireRecord w;
	w.meta.ts = 100;
	w.meta.protocol = "1.0";

	w.record = records::TurnPromptRecord{"turn_1", {llm::TextPart{"a"}}, 0};
	CHECK_OK(fp.Append(w));

	w.record = records::TurnCancelRecord{"turn_1"};
	CHECK_OK(fp.Append(w));

	w.record = records::ContextAppendMessageRecord{MakeUserMessage("second")};
	CHECK_OK(fp.Append(w));

	auto read = fp.Read();
	CHECK(read.ok());
	CHECK_EQ(read->size(), 3u);
	CHECK(std::holds_alternative<records::TurnPromptRecord>((*read)[0].record));
	CHECK(std::holds_alternative<records::TurnCancelRecord>((*read)[1].record));
	CHECK(std::holds_alternative<records::ContextAppendMessageRecord>((*read)[2].record));
}

TEST_CASE("FilePersistence: missing wire.jsonl reads as empty")
{
	TmpDirFixture f;
	records::FilePersistence fp(&f.host, f.wirePath);
	auto read = fp.Read();
	CHECK(read.ok());
	CHECK_EQ(read->size(), 0u);
}

TEST_CASE("FilePersistence: round-trip 4 record types end-to-end")
{
	TmpDirFixture f;

	records::FilePersistence fp(&f.host, f.wirePath);
	records::WireRecord w;
	w.meta.ts = 1;
	w.meta.protocol = "1.0";

	w.record = records::TurnPromptRecord{"t1", {llm::TextPart{"input"}}, 0};
	CHECK_OK(fp.Append(w));

	w.record = records::ContextAppendMessageRecord{MakeUserMessage("hello")};
	CHECK_OK(fp.Append(w));

	engine::AssistantDeltaEvent delta;
	delta.text = "partial";
	w.record = records::ContextAppendLoopEventRecord{std::move(delta)};
	CHECK_OK(fp.Append(w));

	w.record = records::TurnCancelRecord{"t1"};
	CHECK_OK(fp.Append(w));

	auto read = fp.Read();
	CHECK(read.ok());
	REQUIRE_EQ(read->size(), 4u);

	CHECK(std::holds_alternative<records::TurnPromptRecord>((*read)[0].record));
	CHECK(std::holds_alternative<records::ContextAppendMessageRecord>((*read)[1].record));
	CHECK(std::holds_alternative<records::ContextAppendLoopEventRecord>((*read)[2].record));
	CHECK(std::holds_alternative<records::TurnCancelRecord>((*read)[3].record));
}

TEST_CASE("AgentRecords: Log stores and ReadAll returns records")
{
	TmpDirFixture f;

	auto persistence = std::make_unique<records::FilePersistence>(&f.host, f.wirePath);
	records::AgentRecords ar(std::move(persistence));

	CHECK_OK(ar.Log(records::TurnPromptRecord{"turn_1", {llm::TextPart{"hi"}}, 0}));
	CHECK_OK(ar.Log(records::ContextAppendMessageRecord{MakeUserMessage("hello")}));

	auto all = ar.ReadAll();
	CHECK(all.ok());
	REQUIRE_EQ(all->size(), 2u);
	CHECK(std::holds_alternative<records::TurnPromptRecord>((*all)[0].record));
	CHECK(std::holds_alternative<records::ContextAppendMessageRecord>((*all)[1].record));
}

TEST_CASE("AgentRecords: Replay sets restoring flag and is side-effect free")
{
	TmpDirFixture f;

	auto persistence = std::make_unique<records::FilePersistence>(&f.host, f.wirePath);
	records::AgentRecords ar(std::move(persistence));

	CHECK_OK(ar.Log(records::TurnPromptRecord{"t1", {llm::TextPart{"x"}}, 0}));
	CHECK_OK(ar.Log(records::TurnCancelRecord{"t1"}));

	bool sawRestoreDuringReplay = false;
	int count = 0;
	auto status = ar.Replay([&](const records::AgentRecord&) -> absl::Status {
		if (ar.IsRestoring())
			sawRestoreDuringReplay = true;
		++count;
		return absl::OkStatus();
	});

	CHECK_OK(status);
	CHECK(sawRestoreDuringReplay);
	CHECK_FALSE(ar.IsRestoring());
	CHECK_EQ(count, 2);

	// Replay must not have re-recorded the events we just read.
	auto after = ar.ReadAll();
	CHECK(after.ok());
	CHECK_EQ(after->size(), 2u);
}

TEST_CASE("AgentRecords: Replay callback error aborts and clears restoring")
{
	TmpDirFixture f;

	auto persistence = std::make_unique<records::FilePersistence>(&f.host, f.wirePath);
	records::AgentRecords ar(std::move(persistence));

	CHECK_OK(ar.Log(records::TurnPromptRecord{"t1", {llm::TextPart{"x"}}, 0}));
	CHECK_OK(ar.Log(records::TurnPromptRecord{"t2", {llm::TextPart{"y"}}, 0}));

	int count = 0;
	auto status = ar.Replay([&](const records::AgentRecord&) -> absl::Status {
		++count;
		return absl::InternalError("apply failed");
	});

	CHECK_FALSE(status.ok());
	CHECK_FALSE(ar.IsRestoring());
	CHECK_EQ(count, 1);
}
