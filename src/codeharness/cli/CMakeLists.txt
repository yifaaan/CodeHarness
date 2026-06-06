codeharness_add_static_module(codeharness_cli
    cli.cpp
)

target_link_libraries(codeharness_cli
    PUBLIC
        codeharness_foundation
    PRIVATE
        codeharness_commands
        codeharness_config
        codeharness_runtime
        codeharness_tui
        codeharness_ui_backend
)

codeharness_link_first_existing(codeharness_cli PRIVATE CLI11 CLI11::CLI11)
target_link_libraries(codeharness_cli PRIVATE spdlog::spdlog)

add_library(codeharness::cli ALIAS codeharness_cli)
