codeharness_add_static_module(codeharness_engine
    engine.cpp
)

target_link_libraries(codeharness_engine
    PUBLIC
        codeharness_foundation
        codeharness_hooks
        codeharness_permissions
        codeharness_provider
        codeharness_tools
)

add_library(codeharness::engine ALIAS codeharness_engine)
