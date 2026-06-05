codeharness_add_static_module(codeharness_ui_backend
    ui_backend.cpp
)

target_link_libraries(codeharness_ui_backend
    PUBLIC
        codeharness_foundation
        codeharness_runtime
    PRIVATE
        codeharness_commands
)

codeharness_link_first_existing(codeharness_ui_backend PRIVATE nlohmann_json nlohmann_json::nlohmann_json)

add_library(codeharness::ui_backend ALIAS codeharness_ui_backend)
