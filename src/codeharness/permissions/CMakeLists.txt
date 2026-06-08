codeharness_add_static_module(codeharness_permissions
    permission.cpp
)

target_link_libraries(codeharness_permissions PRIVATE spdlog::spdlog)
codeharness_link_first_existing(codeharness_permissions PRIVATE re2 re2::re2)

add_library(codeharness::permissions ALIAS codeharness_permissions)
