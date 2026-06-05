codeharness_add_static_module(codeharness_config
    config.cpp
    config_loader.cpp
    credentials.cpp
    paths.cpp
)

target_link_libraries(codeharness_config
    PUBLIC
        codeharness_foundation
        codeharness_mcp
        codeharness_permissions
    PRIVATE
        spdlog::spdlog
)

codeharness_link_first_existing(codeharness_config PUBLIC nlohmann_json nlohmann_json::nlohmann_json)

add_library(codeharness::config ALIAS codeharness_config)
