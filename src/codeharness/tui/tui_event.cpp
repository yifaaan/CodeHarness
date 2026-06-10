#include "codeharness/tui/tui_event.h"

#include <utility>

namespace codeharness::tui
{

auto TuiEventQueue::push(TuiEvent event) -> bool
{
    std::lock_guard lock{mutex_};
    const auto was_empty = events_.empty();
    events_.push_back(std::move(event));
    return was_empty;
}

auto TuiEventQueue::drain() -> std::vector<TuiEvent>
{
    std::lock_guard lock{mutex_};
    std::vector<TuiEvent> drained;
    drained.reserve(events_.size());
    while (!events_.empty())
    {
        drained.push_back(std::move(events_.front()));
        events_.pop_front();
    }
    return drained;
}

auto TuiEventQueue::empty() const -> bool
{
    std::lock_guard lock{mutex_};
    return events_.empty();
}

TuiEventSender::TuiEventSender(TuiEventQueue& queue, WakeCallback wake_callback)
    : queue_{&queue}
    , wake_callback_{std::move(wake_callback)}
{
}

auto TuiEventSender::send(TuiEvent event) const -> void
{
    const auto should_wake = queue_->push(std::move(event));
    if (should_wake && wake_callback_)
    {
        wake_callback_();
    }
}

} // namespace codeharness::tui
