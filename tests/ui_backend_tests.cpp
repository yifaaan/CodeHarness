#include "codeharness/ui_backend/ui_backend.h"

#include "test_support.h"

#include <nlohmann/json.hpp>

#include <sstream>
#include <string_view>

namespace
{

constexpr std::string_view kBackendPrefix = "OHJSON:";

auto make_bundle(TempDir& temp) -> codeharness::Result<std::unique_ptr<codeharness::runtime::RuntimeBundle>>
{
    const auto repo = temp.path / "repo";
    const auto memory_root = temp.path / "memory-root";
    std::filesystem::create_directories(repo);

    return codeharness::runtime::create_runtime_bundle(
        codeharness::runtime::RuntimeBundleOptions{
            .cwd = repo,
            .memory_root = memory_root,
            .load_default_user_plugins = false,
        });
}

auto parse_backend_output(std::string_view output) -> std::vector<nlohmann::json>
{
    std::vector<nlohmann::json> events;
    std::istringstream stream{std::string{output}};
    std::string line;
    while (std::getline(stream, line))
    {
        REQUIRE(line.starts_with(kBackendPrefix));
        events.push_back(nlohmann::json::parse(line.substr(kBackendPrefix.size())));
    }

    return events;
}

auto run_backend_host(codeharness::runtime::RuntimeBundle& runtime, std::string input, int max_turns = 3)
    -> std::vector<nlohmann::json>
{
    std::istringstream input_stream{std::move(input)};
    std::ostringstream output_stream;
    codeharness::ui_backend::BackendHost host{runtime, input_stream, output_stream, max_turns};

    auto result = host.run();

    REQUIRE(result.has_value());
    return parse_backend_output(output_stream.str());
}

} // namespace

TEST_CASE("ui backend parses frontend requests")
{
    auto request = codeharness::ui_backend::parse_frontend_request(
        R"({"type":"submit_line","line":"hello"})");

    REQUIRE(request.has_value());
    CHECK(request->type == "submit_line");
    REQUIRE(request->line.has_value());
    CHECK(*request->line == "hello");

    auto shutdown = codeharness::ui_backend::parse_frontend_request(R"({"type":"shutdown"})");
    REQUIRE(shutdown.has_value());
    CHECK(shutdown->type == "shutdown");
    CHECK(!shutdown->line.has_value());
}

TEST_CASE("ui backend rejects malformed frontend requests")
{
    auto malformed = codeharness::ui_backend::parse_frontend_request("{");
    REQUIRE(!malformed.has_value());
    CHECK(malformed.error().kind == codeharness::ErrorKind::InvalidArgument);
    CHECK(malformed.error().message.find("failed to parse frontend request") != std::string::npos);

    auto missing_type = codeharness::ui_backend::parse_frontend_request(R"({"line":"hello"})");
    REQUIRE(!missing_type.has_value());
    CHECK(missing_type.error().message.find("requires string field: type") != std::string::npos);

    auto wrong_line_type = codeharness::ui_backend::parse_frontend_request(
        R"({"type":"submit_line","line":42})");
    REQUIRE(!wrong_line_type.has_value());
    CHECK(wrong_line_type.error().message.find("requires string field: line") != std::string::npos);
}

TEST_CASE("ui backend formats OHJSON events")
{
    const auto formatted = codeharness::ui_backend::format_backend_event(
        codeharness::ui_backend::BackendAssistantDelta{.text = "hello"});

    CHECK(formatted.starts_with(kBackendPrefix));
    CHECK(formatted.ends_with('\n'));

    const auto event = nlohmann::json::parse(formatted.substr(kBackendPrefix.size()));
    CHECK(event.at("type") == "assistant_delta");
    CHECK(event.at("text") == "hello");
}

TEST_CASE("ui backend emits errors and shutdown without throwing")
{
    TempDir temp{"codeharness-ui-backend-errors-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        "{\n"
        R"({"type":"unknown"})"
        "\n"
        R"({"type":"submit_line","line":""})"
        "\n"
        R"({"type":"shutdown"})"
        "\n"
        R"({"type":"submit_line","line":"after shutdown"})"
        "\n");

    REQUIRE(events.size() == 5);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "error");
    CHECK(events.at(1).at("message").get<std::string>().find("failed to parse frontend request") != std::string::npos);
    CHECK(events.at(2).at("type") == "error");
    CHECK(events.at(2).at("message") == "unsupported frontend request type: unknown");
    CHECK(events.at(3).at("type") == "error");
    CHECK(events.at(3).at("message") == "submit_line requires non-empty line");
    CHECK(events.at(4).at("type") == "shutdown");
}

TEST_CASE("ui backend submit_line streams assistant delta and line complete")
{
    TempDir temp{"codeharness-ui-backend-submit-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(**bundle, R"({"type":"submit_line","line":"hello backend"})" "\n");

    REQUIRE(events.size() == 3);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "assistant_delta");
    CHECK(events.at(1).at("text") == "hello backend");
    CHECK(events.at(2).at("type") == "line_complete");
}

TEST_CASE("ui backend message-only slash command emits output and completes")
{
    TempDir temp{"codeharness-ui-backend-slash-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(**bundle, R"({"type":"submit_line","line":"/skills"})" "\n");

    REQUIRE(events.size() == 3);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "assistant_delta");
    CHECK(events.at(1).at("text").get<std::string>().find("Available skills:") != std::string::npos);
    CHECK(events.at(2).at("type") == "line_complete");
}

TEST_CASE("ui backend sessions command lists saved sessions")
{
    TempDir temp{"codeharness-ui-backend-sessions-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    codeharness::sessions::SessionSnapshot snapshot;
    snapshot.session_id = "listed";
    snapshot.summary = "listed summary";
    snapshot.model = "echo";
    snapshot.message_count = 2;
    snapshot.created_at = 10.0;
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::User, "listed summary"));
    REQUIRE((*bundle)->sessions().save(snapshot).has_value());

    auto events = run_backend_host(**bundle, R"({"type":"submit_line","line":"/sessions"})" "\n");

    REQUIRE(events.size() == 3);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "assistant_delta");
    const auto text = events.at(1).at("text").get<std::string>();
    CHECK(text.find("Sessions:") != std::string::npos);
    CHECK(text.find("listed") != std::string::npos);
    CHECK(text.find("listed summary") != std::string::npos);
    CHECK(events.at(2).at("type") == "line_complete");
}

TEST_CASE("ui backend resume command loads session without invoking provider")
{
    TempDir temp{"codeharness-ui-backend-resume-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    codeharness::sessions::SessionSnapshot snapshot;
    snapshot.session_id = "resume-backend";
    snapshot.summary = "old work";
    snapshot.message_count = 1;
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::User, "old work"));
    REQUIRE((*bundle)->sessions().save(snapshot).has_value());

    auto events = run_backend_host(**bundle, R"({"type":"submit_line","line":"/resume latest"})" "\n");

    REQUIRE(events.size() == 3);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "assistant_delta");
    CHECK(events.at(1).at("text").get<std::string>().find("Resumed session resume-backend: old work") !=
          std::string::npos);
    CHECK(events.at(2).at("type") == "line_complete");

    auto active = (*bundle)->active_session_summary();
    REQUIRE(active.has_value());
    CHECK(active->session_id == "resume-backend");
}

TEST_CASE("ui backend prompt after resume continues and saves same session")
{
    TempDir temp{"codeharness-ui-backend-resume-continue-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    codeharness::sessions::SessionSnapshot snapshot;
    snapshot.session_id = "same-backend";
    snapshot.summary = "first backend";
    snapshot.created_at = 25.0;
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::User, "first backend"));
    snapshot.messages.push_back(codeharness::make_text_message(codeharness::Role::Assistant, "first answer"));
    snapshot.message_count = static_cast<int>(snapshot.messages.size());
    REQUIRE((*bundle)->sessions().save(snapshot).has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"/resume same-backend"})"
        "\n"
        R"({"type":"submit_line","line":"second backend"})"
        "\n");

    REQUIRE(events.size() == 5);
    CHECK(events.at(1).at("type") == "assistant_delta");
    CHECK(events.at(1).at("text").get<std::string>().find("Resumed session same-backend") != std::string::npos);
    CHECK(events.at(2).at("type") == "line_complete");
    CHECK(events.at(3).at("type") == "assistant_delta");
    CHECK(events.at(3).at("text") == "second backend");
    CHECK(events.at(4).at("type") == "line_complete");

    auto saved = (*bundle)->sessions().load_by_id("same-backend");
    REQUIRE(saved);
    REQUIRE(saved->has_value());
    CHECK((*saved)->session_id == "same-backend");
    CHECK((*saved)->summary == "first backend");
    CHECK((*saved)->created_at == doctest::Approx(25.0));
    CHECK((*saved)->message_count >= 4);
}

TEST_CASE("ui backend resume missing session emits error")
{
    TempDir temp{"codeharness-ui-backend-resume-missing-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(**bundle, R"({"type":"submit_line","line":"/resume missing"})" "\n");

    REQUIRE(events.size() == 2);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "error");
    CHECK(events.at(1).at("message") == "session not found: missing");
}

TEST_CASE("ui backend processes multiple frontend requests in order")
{
    TempDir temp{"codeharness-ui-backend-multiple-test"};
    auto bundle = make_bundle(temp);
    REQUIRE(bundle.has_value());

    auto events = run_backend_host(
        **bundle,
        R"({"type":"submit_line","line":"first"})"
        "\n"
        R"({"type":"submit_line","line":"second"})"
        "\n"
        R"({"type":"shutdown"})"
        "\n");

    REQUIRE(events.size() == 6);
    CHECK(events.at(0).at("type") == "ready");
    CHECK(events.at(1).at("type") == "assistant_delta");
    CHECK(events.at(1).at("text") == "first");
    CHECK(events.at(2).at("type") == "line_complete");
    CHECK(events.at(3).at("type") == "assistant_delta");
    CHECK(events.at(3).at("text") == "second");
    CHECK(events.at(4).at("type") == "line_complete");
    CHECK(events.at(5).at("type") == "shutdown");
}
