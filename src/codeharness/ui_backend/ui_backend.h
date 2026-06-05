#pragma once

#include "codeharness/core/result.h"
#include "codeharness/runtime/runtime.h"

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace codeharness::ui_backend
{

struct FrontendRequest
{
    std::string type;
    std::optional<std::string> line;
};

struct BackendReady
{
};

struct BackendAssistantDelta
{
    std::string text;
};

struct BackendToolStarted
{
    std::string id;
    std::string name;
};

struct BackendToolCompleted
{
    std::string id;
};

struct BackendToolResult
{
    std::string id;
    std::string content;
    bool is_error = false;
};

struct BackendLineComplete
{
};

struct BackendError
{
    std::string message;
};

struct BackendShutdown
{
};

using BackendEvent = std::variant<BackendReady,
                                  BackendAssistantDelta,
                                  BackendToolStarted,
                                  BackendToolCompleted,
                                  BackendToolResult,
                                  BackendLineComplete,
                                  BackendError,
                                  BackendShutdown>;

auto parse_frontend_request(std::string_view line) -> Result<FrontendRequest>;
auto format_backend_event(const BackendEvent& event) -> std::string;

class BackendHost
{
public:
    BackendHost(runtime::RuntimeBundle& runtime, std::istream& input, std::ostream& output, int max_turns);

    auto run() -> Result<void>;

private:
    auto emit(const BackendEvent& event) -> void;
    auto handle_request(const FrontendRequest& request) -> bool;
    auto handle_submit_line(std::string_view line) -> void;
    auto emit_engine_event(const EngineEvent& event) -> void;

    runtime::RuntimeBundle& runtime_;
    std::istream& input_;
    std::ostream& output_;
    int max_turns_ = 10;
};

} // namespace codeharness::ui_backend
