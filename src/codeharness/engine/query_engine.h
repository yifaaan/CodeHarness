#pragma once

#include <absl/types/span.h>

#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

#include "codeharness/api/client.h"
#include "codeharness/engine/message.h"
#include "codeharness/engine/stream_event.h"
#include "codeharness/permissions/checker.h"
#include "codeharness/tools/tool_registry.h"

namespace codeharness::engine {

    class QueryEngine {
    public:
        QueryEngine(api::Client& api, const tools::ToolRegistry& tools,
                    const permissions::PerssionChecker& permissions, std::filesystem::path cwd,
                    std::string model, std::string system_prompt);

        // 用户发消息入口：
        //   把用户 prompt 加入 messages_
        //   调用 api_.stream_message(...)
        //   接收 assistant 的流式文本, 通过 sink 回调通知外部组件
        //   如果模型返回 tool use，就执行工具
        //   把 tool result 再塞回对话
        //   继续循环，直到模型不再请求工具
        auto submit_message(std::string prompt, const api::StreamSink& sink) -> void;

        [[nodiscard]] auto messages() const noexcept -> absl::Span<const ConversationMessage>;
        [[nodiscard]] auto total_usage() const noexcept -> UsageSnapshot;
        auto clear() noexcept -> void;
        auto set_model(std::string model) noexcept -> void;
        auto set_system_prompt(std::string system_prompt) noexcept -> void;

    private:
        // 负责单次工具调用：找工具、查权限、执行工具、包装成 ToolResultBlock
        auto execute_tool_call(const ToolUseBlock& call) -> ToolResultBlock;

        api::Client& api_;
        const tools::ToolRegistry& tools_;
        const permissions::PerssionChecker& permissions_;
        std::filesystem::path cwd_;
        std::string model_;
        std::string system_prompt_;
        int max_tokens_{4096};
        int max_turns_{20};
        std::vector<ConversationMessage> messages_;  // 会话历史
        UsageSnapshot total_usage_;
    };
}  // namespace codeharness::engine