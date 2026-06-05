codeharness_add_static_module(codeharness_runtime
    runtime.cpp
)

target_link_libraries(codeharness_runtime
    PUBLIC
        codeharness_commands
        codeharness_config
        codeharness_coordinator
        codeharness_engine
        codeharness_foundation
        codeharness_memory
        codeharness_permissions
        codeharness_plugins
        codeharness_provider
        codeharness_skills
        codeharness_tools
    PRIVATE
        codeharness_mailbox
        codeharness_prompts
        codeharness_tasks
)

add_library(codeharness::runtime ALIAS codeharness_runtime)
