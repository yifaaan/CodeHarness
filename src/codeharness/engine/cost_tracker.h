#pragma once

#include "codeharness/engine/stream_event.h"

namespace codeharness::engine {

    class CostTracker final {
    public:
        auto add(const UsageSnapshot& usage) noexcept -> void;
        [[nodiscard]] auto total() const noexcept -> UsageSnapshot;
        auto clear() noexcept -> void;

    private:
        UsageSnapshot usage_;
    };

}  // namespace codeharness::engine
