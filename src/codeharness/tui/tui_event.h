#pragma once

#include "codeharness/engine/engine.h"

#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

namespace codeharness::tui
{

struct TuiEngineEvent
{
    EngineEvent event;
};

struct TuiRunCompleted
{
    bool success = false;
    std::string error_message;
    std::string output_text;
    int input_tokens = 0;
    int output_tokens = 0;
};

struct TuiPermissionRequested
{
    PermissionPrompt prompt;
};

struct TuiQuestionRequested
{
    UserQuestionPrompt prompt;
};

struct TuiRefreshRequested
{};

using TuiEvent = std::variant<
    TuiEngineEvent,
    TuiRunCompleted,
    TuiPermissionRequested,
    TuiQuestionRequested,
    TuiRefreshRequested>;

class TuiEventQueue
{
public:
    [[nodiscard]] auto push(TuiEvent event) -> bool;
    [[nodiscard]] auto drain() -> std::vector<TuiEvent>;
    [[nodiscard]] auto empty() const -> bool;

private:
    mutable std::mutex mutex_;
    std::deque<TuiEvent> events_;
};

class TuiEventSender
{
public:
    using WakeCallback = std::function<void()>;

    explicit TuiEventSender(TuiEventQueue& queue, WakeCallback wake_callback = {});

    auto send(TuiEvent event) const -> void;

private:
    TuiEventQueue* queue_;
    WakeCallback wake_callback_;
};

} // namespace codeharness::tui
