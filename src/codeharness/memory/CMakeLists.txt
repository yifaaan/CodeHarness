codeharness_add_static_module(codeharness_memory
    memory_store.cpp
)

target_link_libraries(codeharness_memory
    PUBLIC
        codeharness_foundation
    PRIVATE
        codeharness_tools
)

codeharness_link_first_existing(codeharness_memory PRIVATE date date::date)
codeharness_link_first_existing(codeharness_memory PRIVATE fmt fmt::fmt)
codeharness_link_first_existing(codeharness_memory PRIVATE libgit2 libgit2::libgit2package libgit2::libgit2)
codeharness_link_first_existing(codeharness_memory PRIVATE nlohmann_json nlohmann_json::nlohmann_json)
codeharness_link_first_existing(codeharness_memory PRIVATE yaml-cpp yaml-cpp::yaml-cpp)

add_library(codeharness::memory ALIAS codeharness_memory)
