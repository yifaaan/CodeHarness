add_subdirectory(core)
add_subdirectory(provider)
add_subdirectory(permissions)
add_subdirectory(hooks)
add_subdirectory(tools)
add_subdirectory(skills)
add_subdirectory(plugins)

add_library(codeharness_extensions STATIC)
target_sources(codeharness_extensions PRIVATE
    $<TARGET_OBJECTS:codeharness_skills_objects>
    $<TARGET_OBJECTS:codeharness_plugins_objects>
    tools/skill_tool.cpp
)
target_link_libraries(codeharness_extensions
    PUBLIC
        codeharness_foundation
        codeharness_tools
    PRIVATE
        codeharness_options
)
codeharness_link_first_existing(codeharness_extensions PUBLIC yaml-cpp yaml-cpp::yaml-cpp)
codeharness_link_first_existing(codeharness_extensions PRIVATE fmt fmt::fmt)
codeharness_link_first_existing(codeharness_extensions PRIVATE Glob Glob)
codeharness_link_first_existing(codeharness_extensions PRIVATE nlohmann_json nlohmann_json::nlohmann_json)
codeharness_link_first_existing(codeharness_extensions PRIVATE spdlog spdlog::spdlog)

add_library(codeharness_skills INTERFACE)
target_link_libraries(codeharness_skills INTERFACE codeharness_extensions)

add_library(codeharness_plugins INTERFACE)
target_link_libraries(codeharness_plugins INTERFACE codeharness_extensions)

add_library(codeharness::extensions ALIAS codeharness_extensions)
add_library(codeharness::skills ALIAS codeharness_skills)
add_library(codeharness::plugins ALIAS codeharness_plugins)

add_subdirectory(memory)
add_subdirectory(commands)
add_subdirectory(prompts)
add_subdirectory(engine)
add_subdirectory(cli)

add_library(codeharness_core INTERFACE)
target_link_libraries(codeharness_core
    INTERFACE
        codeharness_cli
        codeharness_commands
        codeharness_engine
        codeharness_extensions
        codeharness_foundation
        codeharness_hooks
        codeharness_memory
        codeharness_permissions
        codeharness_prompts
        codeharness_provider
        codeharness_tools
)

add_library(codeharness::core ALIAS codeharness_core)
