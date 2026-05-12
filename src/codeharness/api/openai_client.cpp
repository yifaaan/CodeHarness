#include "codeharness/api/openai_client.h"

#include <curl/curl.h>
#include <fmt/core.h>

#include <stdexcept>
#include <utility>

#include "codeharness/api/client.h"
#include "codeharness/engine/message.h"

namespace {
    using namespace codeharness;

    auto role_to_openai(engine::MessageRole role) -> std::string {
        return role == engine::MessageRole::user ? "user" : "assistant";
    }

    auto to_openai_tools(const nlohmann::json& tools) -> nlohmann::json {
        auto result = nlohmann::json::array();

        for (const auto& tool : tools) {
            result.push_back({
                {"type", "function"},
                {"function",
                 {
                     {"name", tool.at("name")},
                     {"description", tool.value("description", "")},
                     {"parameters", tool.value("input_schema", nlohmann::json::object())},
                 }},
            });
        }
        return result;
    }

    auto to_openai_messages(const api::MessageRequest& request) -> nlohmann::json {
        auto messages = nlohmann::json::array();
        if (not request.system_prompt.empty()) {
            messages.push_back({
                {"role", "system"},
                {"content", request.system_prompt},
            });
        }

        for (const auto& m : request.messages) {
            std::string text;
            auto tool_calls = nlohmann::json::array();
            for (const auto& block : m.content) {
                if (auto text_block = std::get_if<engine::TextBlock>(&block)) {
                    text += text_block->text;
                }

                // ToolUseBlock{
                //     id = "call_123",
                //     name = "get_weather",
                //     input = { {"city", "Shanghai"} }
                // }

                // "tool_calls": [
                //     {
                //          "id": "call_123",
                //          "type": "function",
                //          "function": {
                //              "name": "get_weather",
                //              "arguments": "{\"city\":\"Shanghai\"}"
                //          }
                //     }
                // ]
                if (auto tool_use = std::get_if<engine::ToolUseBlock>(&block)) {
                    tool_calls.push_back(
                        {{"id", tool_use->id},
                         {"type", "function"},
                         {"function",
                          {{"name", tool_use->name}, "arguments", tool_use->input.dump()}}});
                }
                // ToolResultBlock{
                //     tool_use_id = "call_123",
                //     content = "上海今天 25 度，晴"
                // }

                // {
                //     "role": "tool",
                //     "tool_call_id": "call_123",
                //     "content": "上海今天 25 度，晴"
                // }
                if (auto tool_result = std::get_if<engine::ToolResultBlock>(&block)) {
                    messages.push_back({
                        {"role", "tool"},
                        {"tool_call_id", tool_result->tool_use_id},
                        {"content", tool_result->content},
                    });
                }
            }

            // {
            //     "role": "assistant",
            //     "content": null,
            //     "tool_calls": [
            //         {
            //             "id": "call_123",
            //             "type": "function",
            //             "function": {
            //                 "name": "get_weather",
            //                 "arguments": "{\"city\":\"Shanghai\"}"
            //             }
            //         }
            //     ]
            // }
            if (not text.empty() or not tool_calls.empty()) {
                auto item = nlohmann::json{
                    {"role", role_to_openai(m.role)},
                    {"content", text.empty() ? nlohmann::json(nullptr) : nlohmann::json(text)}};
                if (not tool_calls.empty()) {
                    item["tool_calls"] = std::move(tool_calls);
                }
                messages.push_back(std::move(item));
            }
        }
        return messages;
    }

    // ```
    //  {
    //      "name": "get_weather",
    //      "arguments": "{\"city\":\"Shanghai\",\"unit\":\"celsius\"}"
    //  }
    //
    //  {
    //      "city": "Shanghai",
    //      "unit": "celsius"
    //  }
    // ```
    //
    // 从 OpenAI 返回的 function 对象里取出 arguments 字段，并把它从字符串解析成 JSON
    auto parse_arguments(const nlohmann::json& function) -> nlohmann::json {
        const auto raw = function.value("arguments", "{}");

        try {
            return nlohmann::json::parse(raw);
        } catch (const nlohmann::json::parse_error&) {
            return nlohmann::json::object();
        }
    }

    // {
    //     "choices": [
    //         {
    //              "message": {
    //                  "role": "assistant",
    //                  "content": "我来查一下天气。",
    //                  "tool_calls": [
    //                       {
    //                           "id": "call_123",
    //                           "type": "function",
    //                           "function": {
    //                           "name": "get_weather",
    //                           "arguments": "{\"city\":\"Shanghai\"}"
    //                           }
    //                       }
    //                  ]
    //              },
    //              "finish_reason": "tool_calls"
    //         }
    //     ],
    //     "usage": {
    //         "prompt_tokens": 20,
    //         "completion_tokens": 15
    //     }
    // }
    // 把 OpenAI 返回的完整响应 body 解析成 MessageComplete
    auto parse_message_complete(const nlohmann::json& body) -> api::MessageComplete {
        const auto& choice = body.at("choice").at(0);
        const auto& message = choice.at("message");

        std::vector<engine::ContentBlock> content;
        const auto text = message.value("content", "");
        // TODO:
    }
}  // namespace

namespace codeharness::api {}