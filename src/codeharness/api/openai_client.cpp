#include "codeharness/api/openai_client.h"

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <curl/curl.h>
#include <fmt/core.h>

#include <mutex>
#include <string>
#include <utility>
#include <variant>

#include "codeharness/logging.h"

namespace {
    using namespace codeharness;

    [[nodiscard]] auto ensure_curl_initialized() -> absl::Status {
        static const auto init_status = []() -> absl::Status {
            const auto code = curl_global_init(CURL_GLOBAL_DEFAULT);
            if (code != CURLE_OK) {
                return absl::InternalError(
                    absl::StrCat("curl_global_init failed: ", curl_easy_strerror(code)));
            }
            return absl::OkStatus();
        }();
        return init_status;
    }

    [[nodiscard]] auto trim_trailing_slashes(absl::string_view value) -> std::string_view {
        while (!value.empty() && value.back() == '/') {
            value.remove_suffix(1);
        }
        return value;
    }

    [[nodiscard]] auto chat_completions_url(absl::string_view base_url) -> std::string {
        const auto normalized_base_url = trim_trailing_slashes(base_url);
        if (normalized_base_url.ends_with("/chat/completions")) {
            return std::string{normalized_base_url};
        }
        if (normalized_base_url.ends_with("/v1")) {
            return absl::StrCat(normalized_base_url, "/chat/completions");
        }
        return absl::StrCat(normalized_base_url, "/v1/chat/completions");
    }

    [[nodiscard]] auto to_openai_tools(const nlohmann::json& tools) -> nlohmann::json {
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

    [[nodiscard]] auto to_openai_tool_call(const engine::ToolUseBlock& block) -> nlohmann::json {
        // ToolUseBlock{
        //     id = "call_123",
        //     name = "get_weather",
        //     input = { {"city", "Shanghai"} }
        // }
        //
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
        return {
            {"id", block.id},
            {"type", "function"},
            {"function",
             {
                 {"name", block.name},
                 {"arguments", block.input.dump()},
             }},
        };
    }

    auto append_openai_tool_result(nlohmann::json& messages,
                                   const engine::ToolResultBlock& block) -> void {
        // ToolResultBlock{
        //     tool_use_id = "call_123",
        //     content = "上海今天 25 度，晴"
        // }
        //
        // {
        //     "role": "tool",
        //     "tool_call_id": "call_123",
        //     "content": "上海今天 25 度，晴"
        // }
        messages.push_back({
            {"role", "tool"},
            {"tool_call_id", block.tool_use_id},
            {"content", block.content},
        });
    }

    auto append_openai_message(nlohmann::json& messages,
                               const engine::ConversationMessage& message) -> void {
        std::string text;
        auto tool_calls = nlohmann::json::array();

        for (const auto& block : message.content) {
            if (const auto* item = std::get_if<engine::TextBlock>(&block)) {
                text += item->text;
                continue;
            }

            if (const auto* item = std::get_if<engine::ToolUseBlock>(&block)) {
                tool_calls.push_back(to_openai_tool_call(*item));
                continue;
            }

            if (const auto* item = std::get_if<engine::ToolResultBlock>(&block)) {
                append_openai_tool_result(messages, *item);
            }
        }

        if (text.empty() && tool_calls.empty()) {
            return;
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
        auto item = nlohmann::json{
            {"role", message.role == engine::MessageRole::user ? "user" : "assistant"},
            {"content", text.empty() ? nlohmann::json(nullptr) : nlohmann::json(text)},
        };
        if (!tool_calls.empty()) {
            item["tool_calls"] = std::move(tool_calls);
        }
        messages.push_back(std::move(item));
    }

    [[nodiscard]] auto to_openai_messages(const api::MessageRequest& request) -> nlohmann::json {
        auto messages = nlohmann::json::array();

        if (!request.system_prompt.empty()) {
            messages.push_back({
                {"role", "system"},
                {"content", request.system_prompt},
            });
        }

        for (const auto& message : request.messages) {
            append_openai_message(messages, message);
        }

        return messages;
    }

    // 从 OpenAI 返回的 JSON 对象中获取字符串类型的值，如果值为 null，则返回空字符串
    [[nodiscard]] auto json_string_or_empty(const nlohmann::json& object, absl::string_view key)
        -> std::string {
        const auto key_string = std::string{key};
        if (!object.contains(key_string)) {
            return {};
        }
        const auto& value = object.at(key_string);
        return value.is_string() ? value.get<std::string>() : std::string{};
    }

    // 从 OpenAI 返回的 function 对象里取出 arguments 字段，并把它从字符串解析成 JSON
    [[nodiscard]] auto parse_arguments(const nlohmann::json& function) -> nlohmann::json {
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
        if (!function.contains("arguments")) {
            return nlohmann::json::object();
        }

        const auto& raw = function.at("arguments");
        if (raw.is_object()) {
            return raw;
        }
        if (!raw.is_string()) {
            return nlohmann::json::object();
        }
        const auto raw_string = raw.get<std::string>();

        try {
            return nlohmann::json::parse(raw_string);
        } catch (const nlohmann::json::parse_error&) {
            return nlohmann::json::object();
        }
    }

    [[nodiscard]] auto message_from_openai(const nlohmann::json& message)
        -> absl::StatusOr<engine::ConversationMessage> {
        // OpenAI message:
        //
        // {
        //     "role": "assistant",
        //     "content": "我来查一下天气。",
        //     "tool_calls": [
        //          {
        //              "id": "call_123",
        //              "type": "function",
        //              "function": {
        //                  "name": "get_weather",
        //                  "arguments": "{\"city\":\"Shanghai\"}"
        //              }
        //          }
        //     ]
        // }
        //
        // Internal message JSON content:
        //
        // [
        //     {"type": "text", "text": "我来查一下天气。"},
        //     {
        //         "type": "tool_use",
        //         "id": "call_123",
        //         "name": "get_weather",
        //         "input": {"city": "Shanghai"}
        //     }
        // ]
        auto content = std::vector<engine::ContentBlock>{};

        // 模型返回调用工具的信息时，content 为 null 类型，而不是空字符串类型，这里需要判断
        auto text = json_string_or_empty(message, "content");
        if (!text.empty()) {
            content.emplace_back(engine::TextBlock{.text = std::move(text)});
        }
        
        if (message.contains("tool_calls") && message.at("tool_calls").is_array()) {
            for (const auto& call : message.at("tool_calls")) {
                if (!call.contains("function") || !call.at("function").is_object()) {
                    return absl::InvalidArgumentError(
                        "OpenAI response tool call is missing a function object");
                }
                const auto& function = call.at("function");
                content.emplace_back(engine::ToolUseBlock{
                    .id = json_string_or_empty(call, "id"),
                    .name = json_string_or_empty(function, "name"),
                    .input = parse_arguments(function),
                });
            }
        }

        return engine::ConversationMessage{
            .role = engine::MessageRole::assistant,
            .content = std::move(content),
        };
    }

    [[nodiscard]] auto parse_message_complete(const nlohmann::json& body)
        -> absl::StatusOr<api::MessageComplete> {
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
        //                               "name": "get_weather",
        //                               "arguments": "{\"city\":\"Shanghai\"}"
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
        //         .role = MessageRole::assistant,
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
        //
        // 把 OpenAI 返回的完整响应 body 解析成 MessageComplete
        if (!body.contains("choices") || !body.at("choices").is_array() ||
            body.at("choices").empty()) {
            return absl::InvalidArgumentError("OpenAI response is missing choices[0]");
        }

        const auto& choice = body.at("choices").at(0);
        if (!choice.is_object() || !choice.contains("message") || !choice.at("message").is_object()) {
            return absl::InvalidArgumentError("OpenAI response is missing choice.message");
        }

        const auto& message = choice.at("message");
        const auto usage = body.value("usage", nlohmann::json::object());
        auto parsed_message = message_from_openai(message);
        if (!parsed_message.ok()) {
            return parsed_message.status();
        }

        return api::MessageComplete{
            .message = std::move(*parsed_message),
            .usage =
                engine::UsageSnapshot{
                    .input_tokens = usage.value("prompt_tokens", 0),
                    .output_tokens = usage.value("completion_tokens", 0),
                },
            .stop_reason = choice.value("finish_reason", ""),
        };
    }

    auto write_callback(char* ptr, std::size_t size, std::size_t nmemb, void* userdata)
        -> std::size_t {
        auto* out = static_cast<std::string*>(userdata);
        out->append(ptr, size * nmemb);
        return size * nmemb;
    }

    [[nodiscard]] auto post_json(const api::OpenAIClientOptions& options,
                                 const nlohmann::json& payload) -> absl::StatusOr<nlohmann::json> {
        if (auto init_status = ensure_curl_initialized(); !init_status.ok()) {
            CH_LOG_ERROR("codeharness::api::post_json", "{}", init_status.message());
            return init_status;
        }

        const auto url = chat_completions_url(options.base_url);
        const auto body = payload.dump();
        CH_LOG_DEBUG("codeharness::api::post_json", "url={} request_bytes={} timeout_seconds={}",
                     url, body.size(), options.timeout.count());

        std::string response;

        auto* curl = curl_easy_init();
        if (curl == nullptr) {
            return absl::InternalError("failed to initialize curl");
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
        CH_LOG_DEBUG("codeharness::api::post_json", "response_status={} response_bytes={}",
                     status_code, response.size());

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (result != CURLE_OK) {
            CH_LOG_ERROR("codeharness::api::post_json", "curl request failed error={}",
                         curl_easy_strerror(result));
            return absl::UnavailableError(curl_easy_strerror(result));
        }

        if (status_code < 200 || status_code >= 300) {
            CH_LOG_ERROR("codeharness::api::post_json",
                         "OpenAI-compatible request failed status={} response_bytes={}",
                         status_code, response.size());
            return absl::InternalError(
                fmt::format("OpenAI-compatible request failed: HTTP {}", status_code));
        }

        try {
            CH_LOG_DEBUG("codeharness::api::post_json", "response_body={}", response);
            return nlohmann::json::parse(response);
        } catch (const nlohmann::json::parse_error& error) {
            return absl::InvalidArgumentError(
                absl::StrCat("failed to parse OpenAI response JSON: ", error.what()));
        }
    }
}  // namespace

namespace codeharness::api {

    OpenAIClient::OpenAIClient(OpenAIClientOptions options)
        : options_{std::move(options)} {}

    auto OpenAIClient::stream_message(const MessageRequest& request, ApiStreamSink sink)
        -> absl::Status {
        CH_LOG_DEBUG("OpenAIClient::stream_message", "model={} messages={} tools={} max_tokens={}",
                     request.model, request.messages.size(), request.tools.size(),
                     request.max_tokens);

        auto payload = nlohmann::json{
            {"model", request.model},
            {"messages", to_openai_messages(request)},
            {"max_tokens", request.max_tokens},
            {"stream", false},
        };
        payload["tools"] = to_openai_tools(request.tools);

        auto body = post_json(options_, payload);
        if (!body.ok()) {
            CH_LOG_DEBUG("OpenAIClient::stream_message", "post_json failed status={}",
                         body.status().message());
            return body.status();
        }

        auto complete = parse_message_complete(*body);
        if (!complete.ok()) {
            CH_LOG_DEBUG("OpenAIClient::stream_message",
                         "parse_message_complete failed status={}",
                         complete.status().message());
            return complete.status();
        }
        CH_LOG_DEBUG("OpenAIClient::stream_message",
                     "parsed completion blocks={} input_tokens={} output_tokens={} "
                     "stop_reason={}",
                     complete->message.content.size(), complete->usage.input_tokens,
                     complete->usage.output_tokens, complete->stop_reason);

        if (!complete->message.text().empty()) {
            sink(engine::AssistantTextDelta{.text = complete->message.text()});
        }

        sink(std::move(*complete));
        return absl::OkStatus();
    }

}  // namespace codeharness::api
