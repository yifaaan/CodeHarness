#include "hooks.hpp"

namespace codeharness::hooks {

void HookDispatcher::add_sink(std::unique_ptr<HookSink> sink) {
    sinks_.push_back(std::move(sink));
}

auto HookDispatcher::dispatch(const HookEvent& event) -> bool {
    for (auto& sink : sinks_) {
        if (!sink->emit(event)) return false;
    }
    return true;
}

} // namespace codeharness::hooks
