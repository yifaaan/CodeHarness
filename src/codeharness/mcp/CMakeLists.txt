codeharness_add_static_module(codeharness_mcp
    client_session.cpp
    json_rpc.cpp
    stdio_transport.cpp
    tool_adapter.cpp
)

target_link_libraries(codeharness_mcp
    PUBLIC
        codeharness_foundation
        codeharness_tools
)

codeharness_link_first_existing(codeharness_mcp PUBLIC nlohmann_json nlohmann_json::nlohmann_json)
codeharness_link_first_existing(codeharness_mcp PRIVATE reproc++ reproc++ reproc::reproc++)

add_library(codeharness::mcp ALIAS codeharness_mcp)
