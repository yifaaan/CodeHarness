#include <doctest/doctest.h>

#include "codeharness/engine/cost_tracker.h"

using namespace codeharness;

TEST_CASE("cost tracker accumulates usage snapshots") {
    auto tracker = engine::CostTracker{};

    tracker.add(engine::UsageSnapshot{.input_tokens = 3, .output_tokens = 5});
    tracker.add(engine::UsageSnapshot{.input_tokens = 7, .output_tokens = 11});

    const auto total = tracker.total();
    CHECK(total.input_tokens == 10);
    CHECK(total.output_tokens == 16);
}

TEST_CASE("cost tracker clears accumulated usage") {
    auto tracker = engine::CostTracker{};

    tracker.add(engine::UsageSnapshot{.input_tokens = 3, .output_tokens = 5});
    tracker.clear();

    const auto total = tracker.total();
    CHECK(total.input_tokens == 0);
    CHECK(total.output_tokens == 0);
}
