codeharness_add_static_module(codeharness_provider
    echo_provider.cpp
    provider.cpp
)

target_link_libraries(codeharness_provider PUBLIC codeharness_foundation)

add_library(codeharness::provider ALIAS codeharness_provider)
