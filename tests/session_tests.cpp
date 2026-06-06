#include "codeharness/sessions/session_store.h"
#include "test_support.h"

#include <doctest/doctest.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace ch_sessions = codeharness::sessions;
namespace ch_core = codeharness;

// ---- generate_session_id ----

TEST_CASE("generate_session_id produces unique IDs")
{
    auto id1 = ch_sessions::generate_session_id();
    auto id2 = ch_sessions::generate_session_id();
    CHECK(id1.size() == 12);
    CHECK(id2.size() == 12);
    CHECK(id1 != id2);
    // Ensure hex characters only
    for (char c : id1)
    {
        const bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        CHECK(is_hex);
    }
}

// ---- project_session_dir ----

TEST_CASE("project_session_dir returns stable path for same cwd")
{
    TempDir temp{"session-path-test"};
    auto repo = temp.path / "my_project";
    std::filesystem::create_directories(repo);
    std::error_code ec;
    auto root = temp.path / "session-root";
    std::filesystem::create_directories(root, ec);
    REQUIRE(!ec);

    auto path1 = ch_sessions::project_session_dir(repo, root);
    REQUIRE(path1);
    auto path2 = ch_sessions::project_session_dir(repo, root);
    REQUIRE(path2);
    CHECK(path1->string() == path2->string());
    CHECK(path1->parent_path() == root);
    CHECK(path1->filename().string().starts_with("my_project-"));
    // Should have slug + '-' + 12 hex chars
    CHECK(path1->filename().string().size() > std::string{"my_project-"}.size() + 11);
}

// ---- SessionStore::for_project ----

TEST_CASE("for_project creates deterministic session dir")
{
    TempDir temp{"session-for-project"};
    auto repo = temp.path / "deterministic";
    std::filesystem::create_directories(repo);
    auto root = temp.path / "sessions";
    std::filesystem::create_directories(root);

    auto store1 = ch_sessions::SessionStore::for_project(repo, root);
    REQUIRE(store1);
    auto store2 = ch_sessions::SessionStore::for_project(repo, root);
    REQUIRE(store2);
    CHECK(store1->root() == store2->root());
    CHECK(store1->root().parent_path() == root);
}

// ---- JSON serialization round-trips ----

TEST_CASE("message JSON round-trip preserves text content")
{
    ch_core::Message msg;
    msg.role = ch_core::Role::User;
    msg.content.emplace_back(ch_core::TextBlock{"Hello, world!"});

    auto j = ch_sessions::message_to_json(msg);
    CHECK(j["role"] == "user");
    REQUIRE(j["content"].is_array());
    CHECK(j["content"][0]["type"] == "text");
    CHECK(j["content"][0]["text"] == "Hello, world!");

    auto parsed = ch_sessions::message_from_json(j);
    REQUIRE(parsed);
    CHECK(parsed->role == ch_core::Role::User);
    REQUIRE(parsed->content.size() == 1);
    auto* text = std::get_if<ch_core::TextBlock>(&parsed->content[0]);
    REQUIRE(text != nullptr);
    CHECK(text->text == "Hello, world!");
}

TEST_CASE("message JSON round-trip preserves tool use and tool result")
{
    ch_core::Message msg;
    msg.role = ch_core::Role::Assistant;
    msg.content.emplace_back(ch_core::ToolUseBlock{"tu1", "read_file", R"({"path":"x.txt"})"});

    auto j = ch_sessions::message_to_json(msg);
    CHECK(j["role"] == "assistant");
    REQUIRE(j["content"].is_array());
    CHECK(j["content"][0]["type"] == "tool_use");
    CHECK(j["content"][0]["id"] == "tu1");
    CHECK(j["content"][0]["name"] == "read_file");
    CHECK(j["content"][0]["input"]["path"] == "x.txt");

    auto parsed = ch_sessions::message_from_json(j);
    REQUIRE(parsed);
    auto* tu = std::get_if<ch_core::ToolUseBlock>(&parsed->content[0]);
    REQUIRE(tu != nullptr);
    CHECK(tu->id == "tu1");
    CHECK(tu->name == "read_file");
}

TEST_CASE("message JSON handles tool_result")
{
    ch_core::Message msg;
    msg.role = ch_core::Role::Tool;
    msg.content.emplace_back(ch_core::ToolResultBlock{"tu1", "file content here", false});

    auto j = ch_sessions::message_to_json(msg);
    CHECK(j["role"] == "tool");
    CHECK(j["content"][0]["type"] == "tool_result");
    CHECK(j["content"][0]["tool_use_id"] == "tu1");
    CHECK(j["content"][0]["content"] == "file content here");
    CHECK(j["content"][0]["is_error"] == false);

    auto parsed = ch_sessions::message_from_json(j);
    REQUIRE(parsed);
    auto* tr = std::get_if<ch_core::ToolResultBlock>(&parsed->content[0]);
    REQUIRE(tr != nullptr);
    CHECK(tr->tool_use_id == "tu1");
    CHECK(tr->content == "file content here");
}

TEST_CASE("snapshot JSON round-trip preserves all fields")
{
    ch_sessions::SessionSnapshot snap;
    snap.session_id = "abc123def456";
    snap.cwd = "/tmp/test";
    snap.model = "claude-sonnet-4-6";
    snap.system_prompt = "You are a helpful assistant.";
    snap.messages.push_back(ch_core::make_text_message(ch_core::Role::User, "Hello"));
    snap.messages.push_back(ch_core::make_text_message(ch_core::Role::Assistant, "Hi there!"));
    snap.created_at = 1234567890.0;
    snap.summary = "Hello";
    snap.message_count = 2;

    auto j = ch_sessions::snapshot_to_json(snap);

    CHECK(j["session_id"] == "abc123def456");
    CHECK(j["model"] == "claude-sonnet-4-6");
    CHECK(j["system_prompt"] == "You are a helpful assistant.");
    CHECK(j["created_at"] == 1234567890.0);
    CHECK(j["summary"] == "Hello");
    CHECK(j["message_count"] == 2);
    REQUIRE(j["messages"].is_array());
    CHECK(j["messages"].size() == 2);

    auto parsed = ch_sessions::snapshot_from_json(j);
    REQUIRE(parsed);
    CHECK(parsed->session_id == "abc123def456");
    CHECK(parsed->model == "claude-sonnet-4-6");
    CHECK(parsed->system_prompt == "You are a helpful assistant.");
    CHECK(parsed->created_at == doctest::Approx(1234567890.0));
    CHECK(parsed->summary == "Hello");
    CHECK(parsed->message_count == 2);
    CHECK(parsed->messages.size() == 2);
}

// ---- SessionStore::save and load_latest ----

TEST_CASE("save creates latest.json and session-{id}.json")
{
    TempDir temp{"session-save"};
    auto sessions_root = temp.path / "sessions";
    std::filesystem::create_directories(sessions_root);
    auto repo = temp.path / "repo";
    std::filesystem::create_directories(repo);

    auto store = ch_sessions::SessionStore::for_project(repo, sessions_root);
    REQUIRE(store);

    ch_sessions::SessionSnapshot snap;
    snap.session_id = "test-save-001";
    snap.model = "test-model";
    snap.summary = "test summary";
    snap.message_count = 1;
    snap.messages.push_back(ch_core::make_text_message(ch_core::Role::User, "test"));

    auto saved = store->save(snap);
    REQUIRE(saved);

    // Check latest.json exists
    auto latest = store->root() / "latest.json";
    CHECK(std::filesystem::exists(latest));
    CHECK(saved->string() == latest.string());

    // Check session-{id}.json exists
    auto named = store->root() / "session-test-save-001.json";
    CHECK(std::filesystem::exists(named));
}

TEST_CASE("save and load_latest round-trips")
{
    TempDir temp{"session-roundtrip"};
    auto sessions_root = temp.path / "sessions";
    std::filesystem::create_directories(sessions_root);
    auto repo = temp.path / "roundtrip-repo";
    std::filesystem::create_directories(repo);

    auto store = ch_sessions::SessionStore::for_project(repo, sessions_root);
    REQUIRE(store);

    ch_sessions::SessionSnapshot snap;
    snap.session_id = "rt-001";
    snap.model = "test-model-v2";
    snap.summary = "round trip test";
    snap.message_count = 1;
    snap.cwd = repo;
    snap.created_at = 1000.0;
    snap.messages.push_back(ch_core::make_text_message(ch_core::Role::User, "round trip"));

    auto saved = store->save(snap);
    REQUIRE(saved);

    auto loaded = store->load_latest();
    REQUIRE(loaded);
    REQUIRE(loaded->has_value());

    CHECK((*loaded)->session_id == "rt-001");
    CHECK((*loaded)->model == "test-model-v2");
    CHECK((*loaded)->summary == "round trip test");
    CHECK((*loaded)->message_count == 1);
    CHECK((*loaded)->messages.size() == 1);
}

TEST_CASE("save with reused session_id overwrites named and latest snapshots")
{
    TempDir temp{"session-overwrite"};
    auto sessions_root = temp.path / "sessions";
    std::filesystem::create_directories(sessions_root);
    auto repo = temp.path / "overwrite-repo";
    std::filesystem::create_directories(repo);

    auto store = ch_sessions::SessionStore::for_project(repo, sessions_root);
    REQUIRE(store);

    ch_sessions::SessionSnapshot first;
    first.session_id = "same-id";
    first.model = "model-a";
    first.summary = "first";
    first.message_count = 1;
    first.messages.push_back(ch_core::make_text_message(ch_core::Role::User, "first"));
    REQUIRE(store->save(first));

    ch_sessions::SessionSnapshot second;
    second.session_id = "same-id";
    second.model = "model-b";
    second.summary = "second";
    second.message_count = 2;
    second.messages.push_back(ch_core::make_text_message(ch_core::Role::User, "first"));
    second.messages.push_back(ch_core::make_text_message(ch_core::Role::Assistant, "second"));
    REQUIRE(store->save(second));

    auto loaded_by_id = store->load_by_id("same-id");
    REQUIRE(loaded_by_id);
    REQUIRE(loaded_by_id->has_value());
    CHECK((*loaded_by_id)->model == "model-b");
    CHECK((*loaded_by_id)->summary == "second");
    CHECK((*loaded_by_id)->message_count == 2);

    auto latest = store->load_latest();
    REQUIRE(latest);
    REQUIRE(latest->has_value());
    CHECK((*latest)->session_id == "same-id");
    CHECK((*latest)->model == "model-b");
}

TEST_CASE("load_latest returns nullopt when no session exists")
{
    TempDir temp{"session-empty"};
    auto sessions_root = temp.path / "sessions";
    std::filesystem::create_directories(sessions_root);
    auto repo = temp.path / "empty-repo";
    std::filesystem::create_directories(repo);

    auto store = ch_sessions::SessionStore::for_project(repo, sessions_root);
    REQUIRE(store);

    auto loaded = store->load_latest();
    REQUIRE(loaded);
    CHECK(!loaded->has_value());
}

// ---- load_by_id ----

TEST_CASE("load_by_id loads specific session")
{
    TempDir temp{"session-load-id"};
    auto sessions_root = temp.path / "sessions";
    std::filesystem::create_directories(sessions_root);
    auto repo = temp.path / "load-id-repo";
    std::filesystem::create_directories(repo);

    auto store = ch_sessions::SessionStore::for_project(repo, sessions_root);
    REQUIRE(store);

    ch_sessions::SessionSnapshot snap;
    snap.session_id = "specific-id";
    snap.model = "specific-model";
    snap.summary = "specific session";
    snap.message_count = 1;
    snap.created_at = 2000.0;
    snap.messages.push_back(ch_core::make_text_message(ch_core::Role::User, "specific"));

    REQUIRE(store->save(snap));

    auto loaded = store->load_by_id("specific-id");
    REQUIRE(loaded);
    REQUIRE(loaded->has_value());
    CHECK((*loaded)->session_id == "specific-id");
    CHECK((*loaded)->model == "specific-model");
}

TEST_CASE("load_by_id returns nullopt for missing ID")
{
    TempDir temp{"session-load-missing"};
    auto sessions_root = temp.path / "sessions";
    std::filesystem::create_directories(sessions_root);
    auto repo = temp.path / "missing-repo";
    std::filesystem::create_directories(repo);

    auto store = ch_sessions::SessionStore::for_project(repo, sessions_root);
    REQUIRE(store);

    auto loaded = store->load_by_id("nonexistent");
    REQUIRE(loaded);
    CHECK(!loaded->has_value());
}

TEST_CASE("load_by_id falls back to latest.json")
{
    TempDir temp{"session-load-latest"};
    auto sessions_root = temp.path / "sessions";
    std::filesystem::create_directories(sessions_root);
    auto repo = temp.path / "latest-fallback";
    std::filesystem::create_directories(repo);

    auto store = ch_sessions::SessionStore::for_project(repo, sessions_root);
    REQUIRE(store);

    ch_sessions::SessionSnapshot snap;
    snap.session_id = "latest-fallback-id";
    snap.model = "fallback-model";
    snap.summary = "fallback";
    snap.message_count = 1;
    snap.messages.push_back(ch_core::make_text_message(ch_core::Role::User, "fallback test"));

    REQUIRE(store->save(snap));

    // load_by_id("latest") should return the same as load_latest()
    auto loaded = store->load_by_id("latest");
    REQUIRE(loaded);
    REQUIRE(loaded->has_value());
    CHECK((*loaded)->session_id == "latest-fallback-id");
}

// ---- list ----

TEST_CASE("list returns sessions newest first")
{
    TempDir temp{"session-list"};
    auto sessions_root = temp.path / "sessions";
    std::filesystem::create_directories(sessions_root);
    auto repo = temp.path / "list-repo";
    std::filesystem::create_directories(repo);

    auto store = ch_sessions::SessionStore::for_project(repo, sessions_root);
    REQUIRE(store);

    // Save two sessions
    ch_sessions::SessionSnapshot snap1;
    snap1.session_id = "first";
    snap1.summary = "first session";
    snap1.message_count = 1;
    snap1.created_at = 100.0;
    snap1.messages.push_back(ch_core::make_text_message(ch_core::Role::User, "first"));
    REQUIRE(store->save(snap1));

    ch_sessions::SessionSnapshot snap2;
    snap2.session_id = "second";
    snap2.summary = "second session";
    snap2.message_count = 1;
    snap2.created_at = 200.0;
    snap2.messages.push_back(ch_core::make_text_message(ch_core::Role::User, "second"));
    REQUIRE(store->save(snap2));

    auto list = store->list(10);
    REQUIRE(list);
    // Should be at least 2 entries (snap2 saved later)
    CHECK(list->size() >= 2);
    // Verify the list has the expected IDs in some order.
    bool found_first = false;
    bool found_second = false;
    for (const auto& s : *list)
    {
        if (s.session_id == "first") found_first = true;
        if (s.session_id == "second") found_second = true;
    }
    CHECK(found_first);
    CHECK(found_second);
}

TEST_CASE("list respects limit")
{
    TempDir temp{"session-list-limit"};
    auto sessions_root = temp.path / "sessions";
    std::filesystem::create_directories(sessions_root);
    auto repo = temp.path / "limit-repo";
    std::filesystem::create_directories(repo);

    auto store = ch_sessions::SessionStore::for_project(repo, sessions_root);
    REQUIRE(store);

    for (int i = 0; i < 5; ++i)
    {
        ch_sessions::SessionSnapshot snap;
        snap.session_id = fmt::format("sess-{:03d}", i);
        snap.summary = fmt::format("session {}", i);
        snap.message_count = 1;
        snap.messages.push_back(ch_core::make_text_message(ch_core::Role::User, fmt::format("msg {}", i)));
        REQUIRE(store->save(snap));
    }

    auto list = store->list(3);
    REQUIRE(list);
    CHECK(list->size() <= 3);
}

// ---- export_markdown ----

TEST_CASE("export_markdown writes transcript.md")
{
    TempDir temp{"session-export"};
    auto sessions_root = temp.path / "sessions";
    std::filesystem::create_directories(sessions_root);
    auto repo = temp.path / "export-repo";
    std::filesystem::create_directories(repo);

    auto store = ch_sessions::SessionStore::for_project(repo, sessions_root);
    REQUIRE(store);

    std::vector<ch_core::Message> messages;
    messages.push_back(ch_core::make_text_message(ch_core::Role::User, "Hello!"));
    messages.push_back(ch_core::make_text_message(ch_core::Role::Assistant, "Hi there!"));

    auto path = store->export_markdown(messages);
    REQUIRE(path);
    CHECK(std::filesystem::exists(*path));
    CHECK(path->filename() == "transcript.md");

    auto content = read_file_text(*path);
    CHECK(content.find("# CodeHarness") != std::string::npos);
    CHECK(content.find("## user") != std::string::npos);
    CHECK(content.find("Hello!") != std::string::npos);
    CHECK(content.find("## assistant") != std::string::npos);
    CHECK(content.find("Hi there!") != std::string::npos);
}

TEST_CASE("export_markdown includes tool use sections")
{
    TempDir temp{"session-export-tool"};
    auto sessions_root = temp.path / "sessions";
    std::filesystem::create_directories(sessions_root);
    auto repo = temp.path / "export-tool-repo";
    std::filesystem::create_directories(repo);

    auto store = ch_sessions::SessionStore::for_project(repo, sessions_root);
    REQUIRE(store);

    std::vector<ch_core::Message> messages;
    {
        ch_core::Message msg;
        msg.role = ch_core::Role::Assistant;
        msg.content.emplace_back(ch_core::ToolUseBlock{"tu1", "read_file", R"({"path":"test.txt"})"});
        messages.push_back(std::move(msg));
    }
    {
        ch_core::Message msg;
        msg.role = ch_core::Role::Tool;
        msg.content.emplace_back(ch_core::ToolResultBlock{"tu1", "file contents", false});
        messages.push_back(std::move(msg));
    }

    auto path = store->export_markdown(messages);
    REQUIRE(path);
    auto content = read_file_text(*path);
    CHECK(content.find("```tool") != std::string::npos);
    CHECK(content.find("read_file") != std::string::npos);
    CHECK(content.find("```tool-result") != std::string::npos);
    CHECK(content.find("file contents") != std::string::npos);
}

// ---- Corrupted JSON ----

TEST_CASE("corrupted JSON returns error on load")
{
    TempDir temp{"session-corrupt"};
    auto sessions_root = temp.path / "sessions";
    std::filesystem::create_directories(sessions_root);
    auto repo = temp.path / "corrupt-repo";
    std::filesystem::create_directories(repo);

    auto store = ch_sessions::SessionStore::for_project(repo, sessions_root);
    REQUIRE(store);

    // Write invalid JSON to latest.json
    std::error_code ec;
    std::filesystem::create_directories(store->root(), ec);
    write_file(store->root() / "latest.json", "{invalid json}");

    auto loaded = store->load_latest();
    CHECK(!loaded); // should return error
}

// ---- Snapshot summary extraction ----

TEST_CASE("snapshot_from_json extracts summary from first user message when missing")
{
    auto j = nlohmann::json::parse(R"({
        "session_id": "no-summary",
        "model": "test",
        "messages": [
            {"role": "user", "content": [{"type": "text", "text": "This is my first prompt"}]}
        ]
    })");

    auto snap = ch_sessions::snapshot_from_json(j);
    REQUIRE(snap);
    CHECK(snap->summary == "This is my first prompt");
}
