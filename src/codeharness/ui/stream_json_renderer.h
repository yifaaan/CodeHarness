#pragma once

#include <nlohmann/json.hpp>

#include "codeharness/engine/stream_event.h"


namespace codeharness::ui {

    [[nodiscard]] auto to_stream_json(const engine::StreamEvent& event) -> nlohmann::json;

}  // namespace codeharness::ui
