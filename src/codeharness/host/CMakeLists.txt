codeharness_add_static_module(codeharness_host
    local_host.cpp
)

target_link_libraries(codeharness_host PUBLIC codeharness_foundation)
codeharness_link_first_existing(codeharness_host PRIVATE Glob Glob)
codeharness_link_first_existing(codeharness_host PRIVATE reproc++ reproc++ reproc::reproc++)
target_link_libraries(codeharness_host PRIVATE spdlog::spdlog)
target_link_libraries(codeharness_host PUBLIC absl::status absl::statusor)

add_library(codeharness::host ALIAS codeharness_host)
