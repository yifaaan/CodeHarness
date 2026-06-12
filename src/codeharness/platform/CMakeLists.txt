codeharness_add_static_module(codeharness_platform
    local_platform.cpp
)

target_link_libraries(codeharness_platform PUBLIC codeharness_foundation)
codeharness_link_first_existing(codeharness_platform PRIVATE Glob Glob)
codeharness_link_first_existing(codeharness_platform PRIVATE reproc++ reproc++ reproc::reproc++)
target_link_libraries(codeharness_platform PRIVATE spdlog::spdlog)
# abseil for absl::Status / absl::StatusOr
target_link_libraries(codeharness_platform PUBLIC absl::status absl::statusor)

add_library(codeharness::platform ALIAS codeharness_platform)
