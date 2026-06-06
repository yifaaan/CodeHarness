codeharness_add_static_module(codeharness_sessions
    session_store.cpp
)

target_link_libraries(codeharness_sessions
    PUBLIC
        codeharness_foundation
        codeharness_config
    PRIVATE
        codeharness_tools
)

codeharness_link_first_existing(codeharness_sessions PRIVATE libgit2 libgit2::libgit2package libgit2::libgit2)
codeharness_link_first_existing(codeharness_sessions PRIVATE nlohmann_json nlohmann_json::nlohmann_json)
codeharness_link_first_existing(codeharness_sessions PRIVATE fmt fmt::fmt)
codeharness_link_first_existing(codeharness_sessions PRIVATE spdlog spdlog::spdlog)

add_library(codeharness::sessions ALIAS codeharness_sessions)
