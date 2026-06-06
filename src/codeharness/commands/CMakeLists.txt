codeharness_add_static_module(codeharness_commands
    command_registry.cpp
)

target_link_libraries(codeharness_commands
    PUBLIC
        codeharness_extensions
        codeharness_foundation
        codeharness_sessions
    PRIVATE
        codeharness_memory
)

add_library(codeharness::commands ALIAS codeharness_commands)
