#include <doctest/doctest.h>

#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <variant>
#include <vector>

#include "codeharness/api/mock_client.h"
#include "codeharness/engine/message.h"
#include "codeharness/engine/query_engine.h"
#include "codeharness/engine/stream_event.h"
#include "codeharness/permissions/checker.h"
#include "codeharness/permissions/models.h"
#include "codeharness/tools/read_file_tool.h"
#include "codeharness/tools/tool_registry.h"

using namespace codeharness;

TEST_CASE("query engine executes read_file through mock api") {
    const auto cwd = std::filesystem::temp_directory_path() / "codeharness-query-engine-test";
    std::filesystem::create_directories(cwd);
    const auto file_path = cwd / "hello.text";
    {
        std::ofstream file{file_path};
        file << "alpha\nbeta\n";
    }

    auto
        responses =
            std::deque<api::MockClient::Response>{
                api::MockClient::Response{
                    .message =
                        engine::ConversationMessage{
                            .role = engine::MessageRole::assistant,
                            .content =
                                {
                                    engine::TextBlock{.text = "I will read the file."},
                                    engine::ToolUseBlock{
                                        .id = "toolu_read_1",
                                        .name = "read_file",
                                        .input = nlohmann::json{{"path", "hello.text"}},
                                    },
                                },
                        },
                    .usage =
                        engine::UsageSnapshot{
                            .input_tokens = 4,
                            .output_tokens = 3,
                        },
                },
                api::MockClient::Response{
                    .message =
                        engine::ConversationMessage{
                            .role = engine::MessageRole::assistant,
                            .content =
                                {
                                    engine::TextBlock{.text = "The file contains alpha and beta."},
                                },
                        },
                    .usage =
                        engine::UsageSnapshot{
                            .input_tokens = 8,
                            .output_tokens = 6,
                        },
                }};
    auto client = api::MockClient{std::move(responses)};
    auto registry = tools::ToolRegistry{};
    registry.register_tool(std::make_unique<tools::ReadFileTool>());
    auto perm_settings = permissions::PermissionSettings{
        .mode = permissions::PermissionMode::default_mode,
    };
    auto permissions = permissions::PermissionChecker{std::move(perm_settings)};

    auto engine =
        engine::QueryEngine{client, registry, permissions, cwd, "mock-model", "system-prompt"};

    std::vector<engine::StreamEvent> events;

    REQUIRE(engine
                .submit_message("read hello.text",
                                [&](const engine::StreamEvent& event) { events.push_back(event); })
                .ok());

    // user input : read hello.txt
    // -> llm 返回 tool_use:read_file
    // -> 执行 ReadFileTool
    // -> 将结果添加到历史消息
    // -> 再发
    // -> 最终结果
    CHECK(client.requests().size() == 2);
    CHECK(engine.messages().size() == 4);

    CHECK(engine.total_usage().input_tokens == 12);
    CHECK(engine.total_usage().output_tokens == 9);

    const auto has_tool_start = std::ranges::any_of(events, [](const auto& event) {
        return std::holds_alternative<engine::ToolExecutionStared>(event);
    });

    const auto has_tool_complete = std::ranges::any_of(events, [](const auto& event) {
        return std::holds_alternative<engine::ToolExecutionComplete>(event);
    });

    const auto has_final_turn = std::ranges::any_of(events, [](const auto& event) {
        auto complete = std::get_if<engine::AssistantTurnComplete>(&event);
        return complete != nullptr and
               complete->message.text() == "The file contains alpha and beta.";
    });

    CHECK(has_tool_start);
    CHECK(has_tool_complete);
    CHECK(has_final_turn);

    std::filesystem::remove_all(cwd);
}
