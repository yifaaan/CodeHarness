codeharness_add_static_module(codeharness_permissions
    permission.cpp
)

target_link_libraries(codeharness_permissions PRIVATE spdlog::spdlog)

add_library(codeharness::permissions ALIAS codeharness_permissions)
