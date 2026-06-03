codeharness_add_static_module(codeharness_cli
    cli.cpp
)

target_link_libraries(codeharness_cli
    PUBLIC
        codeharness_foundation
        codeharness_memory
        codeharness_prompts
    PRIVATE
        codeharness_commands
        codeharness_engine
        codeharness_extensions
        codeharness_provider
        codeharness_tasks
        codeharness_tools
)

codeharness_link_first_existing(codeharness_cli PRIVATE CLI11 CLI11::CLI11)
codeharness_link_first_existing(codeharness_cli PRIVATE spdlog spdlog::spdlog)

add_library(codeharness::cli ALIAS codeharness_cli)
