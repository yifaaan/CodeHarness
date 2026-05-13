#include "codeharness/ui/stream_json_renderer.h"

namespace codeharness::ui {
    auto to_stream_json(const engine::StreamEvent& event) -> nlohmann::json {
        return event;
    }

}  // namespace codeharness::ui
