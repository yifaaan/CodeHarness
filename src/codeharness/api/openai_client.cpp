#include "codeharness/api/openai_client.h"

#include <curl/curl.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <stdexcept>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "codeharness/api/client.h"
#include "codeharness/engine/message.h"
#include "codeharness/engine/stream_event.h"
#include "curl/easy.h"
#include "nlohmann/json_fwd.hpp"

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
    //
    // MessageComplete{
    //     .message = ConversationMessage{
    //         .role = MessageRole::assistent,
    //         .content = {
    //             TextBlock{ .text = "我来查一下天气。" },
    //             ToolUseBlock{
    //                 .id = "call_123",
    //                 .name = "get_weather",
    //                 .input = { {"city", "Shanghai"} }
    //             }
    //         }
    //     },
    //     .usage = UsageSnapshot{
    //         .input_tokens = 20,
    //         .output_tokens = 15
    //     },
    //     .stop_reason = "tool_calls"
    // }

    // 把 OpenAI 返回的完整响应 body 解析成 MessageComplete
    auto parse_message_complete(const nlohmann::json& body) -> api::MessageComplete {
        const auto& choice = body.at("choice").at(0);
        const auto& message = choice.at("message");

        std::vector<engine::ContentBlock> content;
        const auto text = message.value("content", "");
        if (!text.empty()) {
            content.emplace_back(engine::TextBlock{.text = text});
        }
        if (message.contains("tool_calls")) {
            for (const auto& call : message.at("tool_calls")) {
                const auto& func = call.at("function");
                content.emplace_back(engine::ToolUseBlock{.id = call.value("id", ""),
                                                          .name = func.value("name", ""),
                                                          .input = parse_arguments(func)});
            }
        }
        const auto usage = body.value("usage", nlohmann::json::object());

        return api::MessageComplete{
            .message =
                engine::ConversationMessage{
                    .role = engine::MessageRole::assistent,
                    .content = std::move(content),
                },
            .usage =
                engine::UsageSnapshot{
                    .input_tokens = usage.value("prompt_tokens", 0),
                    .output_tokens = usage.value("completion_tokens", 0),
                },
            .stop_reason = choice.value("finish_reason", ""),
        };
        // TODO:
    }

    auto write_callback(char* ptr, std::size_t size, std::size_t nmemb, void* userdata)
        -> std::size_t {
        auto* out = static_cast<std::string*>(userdata);
        out->append(ptr, size * nmemb);
        return size * nmemb;
    }

    auto post_json(const api::OpenAIClientOptions& options, const nlohmann::json& payload)
        -> nlohmann::json {
        // const auto url = absl::StrCat(options.base_url, "/chat/completions");
        const auto url = absl::StrCat(options.base_url, "");
        const auto body = payload.dump();
        spdlog::debug("api: POST {} request_bytes={} timeout_seconds={}", url, body.size(),
                      options.timeout.count());

        std::string response;

        auto curl = curl_easy_init();
        if (not curl) {
            throw std::runtime_error{"failed to initialize curl"};
        }

        curl_slist* headers{};
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(
            headers, absl::StrFormat("Authorization: Bearer %s", options.api_key).c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(options.timeout.count()));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        const auto result = curl_easy_perform(curl);

        long status_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        spdlog::debug("api: response status={} bytes={}", status_code, response.size());

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (result != CURLE_OK) {
            spdlog::error("api: curl request failed: {}", curl_easy_strerror(result));
            throw std::runtime_error{curl_easy_strerror(result)};
        }

        if (status_code < 200 || status_code >= 300) {
            spdlog::error("api: OpenAI-compatible request failed status={} response_bytes={}",
                          status_code, response.size());
            throw std::runtime_error{
                fmt::format("OpenAI-compatible request failed: HTTP {}", status_code)};
        }

        return nlohmann::json::parse(response);
    }
}  // namespace

namespace codeharness::api {
    OpenAIClient::OpenAIClient(OpenAIClientOptions options) : options_{std::move(options)} {}

    void OpenAIClient::stream_message(const MessageRequest& request, ApiStreamSink sink) {
        spdlog::debug("api: stream_message model={} messages={} tools={} max_tokens={}",
                      request.model, request.messages.size(), request.tools.size(),
                      request.max_tokens);
        auto payload = nlohmann::json{{"model", request.model},
                                      {"messages", to_openai_messages(request)},
                                      {"max_tokens", request.max_tokens},
                                      {"stream", false}};
        if (!request.tools.empty()) {
            payload["tools"] = to_openai_tools(request.tools);
        }

        const auto body = post_json(options_, payload);
        auto complete = parse_message_complete(body);
        spdlog::debug(
            "api: parsed completion blocks={} input_tokens={} output_tokens={} stop_reason={}",
            complete.message.content.size(), complete.usage.input_tokens,
            complete.usage.output_tokens, complete.stop_reason);

        if (!complete.message.text().empty()) {
            sink(engine::AssistantTextDelta{.text = complete.message.text()});
        }

        sink(std::move(complete));
    }
}  // namespace codeharness::api
