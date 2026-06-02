codeharness_add_static_module(codeharness_permissions
    permission.cpp
)

codeharness_link_first_existing(codeharness_permissions PRIVATE spdlog spdlog::spdlog)

add_library(codeharness::permissions ALIAS codeharness_permissions)
