codeharness_add_static_module(codeharness_provider
    echo_provider.cpp
    provider.cpp
    provider_http.cpp
    provider_tool_schema.cpp
    anthropic_serialize.cpp
    anthropic_stream_parser.cpp
    anthropic_provider.cpp
    openai_serialize.cpp
    openai_stream_parser.cpp
    openai_provider.cpp
)

target_link_libraries(codeharness_provider
    PUBLIC
        codeharness_foundation
        codeharness_network_objects
)

codeharness_link_first_existing(codeharness_provider PUBLIC nlohmann_json nlohmann_json::nlohmann_json)

add_library(codeharness::provider ALIAS codeharness_provider)
