#include "codeharness/engine/cost_tracker.h"

namespace codeharness::engine {

    auto CostTracker::add(const UsageSnapshot& usage) noexcept -> void {
        usage_.input_tokens += usage.input_tokens;
        usage_.output_tokens += usage.output_tokens;
    }

    auto CostTracker::total() const noexcept -> UsageSnapshot { return usage_; }

    auto CostTracker::clear() noexcept -> void { usage_ = UsageSnapshot{}; }

}  // namespace codeharness::engine
