#pragma once

#include <string>
#include <memory>
#include <vector>
#include <chrono>

namespace codeharness::hooks {

enum class HookEventType {
    ResponseStart,
    ResponseDelta,
    ResponseEnd,
    ToolLifecycle,
    JobLifecycle,
    ApprovalLifecycle,
    GenericEvent
};

struct HookEvent {
    HookEventType type;
    std::string payload;
    std::chrono::system_clock::time_point timestamp;
};

class HookSink {
public:
    virtual ~HookSink() = default;
    virtual auto emit(const HookEvent& event) -> bool = 0;
};

class HookDispatcher {
public:
    void add_sink(std::unique_ptr<HookSink> sink);
    auto dispatch(const HookEvent& event) -> bool;

private:
    std::vector<std::unique_ptr<HookSink>> sinks_;
};

} // namespace codeharness::hooks
