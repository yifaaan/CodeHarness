codeharness_add_static_module(codeharness_hooks
    hook_config.cpp
    hook_executor.cpp
    hook_registry.cpp
)

target_link_libraries(codeharness_hooks PUBLIC codeharness_foundation)
codeharness_link_first_existing(codeharness_hooks PUBLIC nlohmann_json nlohmann_json::nlohmann_json)
codeharness_link_first_existing(codeharness_hooks PRIVATE fmt fmt::fmt)
codeharness_link_first_existing(codeharness_hooks PRIVATE reproc++ reproc++ reproc::reproc++)

add_library(codeharness::hooks ALIAS codeharness_hooks)
