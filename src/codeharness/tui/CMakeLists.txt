codeharness_add_static_module(codeharness_tui
    tui_app.cpp
    tui_composer.cpp
    tui_markdown.cpp
    tui_render.cpp
)

target_link_libraries(codeharness_tui
    PUBLIC
        codeharness_foundation
        codeharness_runtime
    PRIVATE
        codeharness_engine
)

target_link_libraries(codeharness_tui PRIVATE ftxui::component ftxui::screen ftxui::dom)

add_library(codeharness::tui ALIAS codeharness_tui)
