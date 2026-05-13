#include <doctest/doctest.h>

#include <nlohmann/json.hpp>
#include <string>

#include "codeharness/engine/message.h"
#include "codeharness/engine/message_json.h"

using namespace codeharness;

TEST_CASE("conversation message json round trips all content block types") {
    const auto original = engine::ConversationMessage{
        .role = engine::MessageRole::assistant,
        .content =
            {
                engine::TextBlock{.text = "I will inspect the file."},
                engine::ToolUseBlock{
                    .id = "toolu_1",
                    .name = "read_file",
                    .input = nlohmann::json{{"path", "hello.txt"}},
                },
                engine::ToolResultBlock{
                    .tool_use_id = "toolu_1",
                    .content = "alpha\nbeta\n",
                    .is_error = false,
                },
            },
    };

    const auto serialized = nlohmann::json(original);

    CHECK(serialized.at("role").get<std::string>() == "assistant");
    REQUIRE(serialized.at("content").size() == 3);
    CHECK(serialized.at("content").at(0).at("type").get<std::string>() == "text");
    CHECK(serialized.at("content").at(1).at("type").get<std::string>() == "tool_use");
    CHECK(serialized.at("content").at(2).at("type").get<std::string>() == "tool_result");

    const auto parsed = engine::conversation_message_from_json(serialized);
    REQUIRE(parsed.ok());

    CHECK(parsed->role == engine::MessageRole::assistant);
    REQUIRE(parsed->content.size() == 3);
    CHECK(parsed->text() == "I will inspect the file.");

    const auto* tool_use = std::get_if<engine::ToolUseBlock>(&parsed->content[1]);
    REQUIRE(tool_use != nullptr);
    CHECK(tool_use->id == "toolu_1");
    CHECK(tool_use->name == "read_file");
    CHECK(tool_use->input.at("path").get<std::string>() == "hello.txt");

    const auto* tool_result = std::get_if<engine::ToolResultBlock>(&parsed->content[2]);
    REQUIRE(tool_result != nullptr);
    CHECK(tool_result->tool_use_id == "toolu_1");
    CHECK(tool_result->content == "alpha\nbeta\n");
    CHECK_FALSE(tool_result->is_error);
}

TEST_CASE("conversation message json accepts legacy assistant spelling") {
    const auto parsed = engine::conversation_message_from_json(nlohmann::json{
        {"role", "assistant"},
        {"content", nlohmann::json::array({{{"type", "text"}, {"text", "hello"}}})},
    });
    REQUIRE(parsed.ok());

    CHECK(parsed->role == engine::MessageRole::assistant);
    CHECK(parsed->text() == "hello");
}
