codeharness_add_static_module(codeharness_gateway
    bridge.cpp
    message_bus.cpp
    runtime_pool.cpp
    service.cpp
    stdio_adapter.cpp
)

target_link_libraries(codeharness_gateway
    PUBLIC
        codeharness_foundation
)

add_library(codeharness::gateway ALIAS codeharness_gateway)
