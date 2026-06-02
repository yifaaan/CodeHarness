codeharness_add_static_module(codeharness_prompts
    project_context.cpp
    system_prompt.cpp
)

target_link_libraries(codeharness_prompts
    PUBLIC
        codeharness_commands
        codeharness_extensions
        codeharness_foundation
        codeharness_permissions
    PRIVATE
        codeharness_tools
)

codeharness_link_first_existing(codeharness_prompts PRIVATE date date::date)
codeharness_link_first_existing(codeharness_prompts PRIVATE libgit2 libgit2::libgit2package libgit2::libgit2)

add_library(codeharness::prompts ALIAS codeharness_prompts)
