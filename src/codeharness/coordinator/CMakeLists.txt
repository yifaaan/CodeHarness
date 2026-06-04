codeharness_add_static_module(codeharness_coordinator
    agent_definition.cpp
)

target_link_libraries(codeharness_coordinator
    PUBLIC
        codeharness_foundation
    PRIVATE
        codeharness_tools
)

codeharness_link_first_existing(codeharness_coordinator PRIVATE Glob Glob)
codeharness_link_first_existing(codeharness_coordinator PRIVATE yaml-cpp yaml-cpp::yaml-cpp)

add_library(codeharness::coordinator ALIAS codeharness_coordinator)
