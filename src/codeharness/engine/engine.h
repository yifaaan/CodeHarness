#pragma once

#include "codeharness/core/message.h"
#include "codeharness/core/result.h"
#include "codeharness/hooks/hook_executor.h"
#include "codeharness/permissions/permission.h"
#include "codeharness/provider/provider.h"
#include "codeharness/tools/tool_registry.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace codeharness
{

struct EngineOptions
{
    int max_turns = 10;
};

struct RunRequest
{
    std::string prompt;
    std::optional<std::string> system_prompt;
    std::optional<std::vector<Message>> initial_messages;
    EngineOptions options;
};

struct RunResult
{
    std::vector<Message> messages;
    std::string output_text;
};

struct EngineAssistantTextDelta
{
    std::string text;
};

struct EngineToolStarted
{
    std::string id;
    std::string name;
};

struct EngineToolFinished
{
    std::string id;
};

struct EngineToolResult
{
    std::string id;
    std::string content;
    bool is_error = false;
};

struct EngineError
{
    std::string message;
};

// Events emitted by the engine during a run, which can be consumed by the caller to get intermediate results or update
// UI.
using EngineEvent =
    std::variant<EngineAssistantTextDelta, EngineToolStarted, EngineToolFinished, EngineToolResult, EngineError>;

using EngineEventSink = std::function<void(const EngineEvent&)>;

class Engine
{
public:
    Engine(
        Provider& provider,
        ToolRegistry& tools,
        const PermissionChecker* permissions = nullptr,
        const HookExecutor* hooks = nullptr);

    auto run(const RunRequest& request) -> Result<RunResult>;
    auto run_streaming(const RunRequest& request, const EngineEventSink& sink) -> Result<RunResult>;

private:
    auto execute_tool_use(const ToolUseBlock& tool_use) -> ToolResultBlock;

    // Streams a single turn of the provider, returning the final assistant message once MessageFinished is received.
    auto stream_provider_turn(std::span<const Message> messages, const EngineEventSink& sink) const -> Result<Message>;

    Provider& provider_;
    ToolRegistry& tools_;
    const PermissionChecker* permissions_ = nullptr;
    const HookExecutor* hooks_ = nullptr;
};

} // namespace codeharness
