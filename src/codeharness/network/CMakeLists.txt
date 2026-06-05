codeharness_add_object_module(codeharness_network_objects
    http_client.h http_client.cpp
    sse_parser.h sse_parser.cpp
)

target_link_libraries(codeharness_network_objects
    PUBLIC
        codeharness_foundation
        codeharness_options
        Boost::beast
        Boost::url
        OpenSSL::SSL
        OpenSSL::Crypto
)

add_library(codeharness_network INTERFACE)
target_link_libraries(codeharness_network INTERFACE codeharness_network_objects)
add_library(codeharness::network ALIAS codeharness_network)
