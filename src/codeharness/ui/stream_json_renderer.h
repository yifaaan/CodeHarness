#pragma once

#include <nlohmann/json.hpp>

#include "codeharness/engine/stream_event.h"


namespace codeharness::ui {

    // Thin alias: StreamEvent already provides nlohmann ADL serialization.
    [[nodiscard]] inline auto to_stream_json(const engine::StreamEvent& event) -> nlohmann::json {
        return event;
    }

}  // namespace codeharness::ui
