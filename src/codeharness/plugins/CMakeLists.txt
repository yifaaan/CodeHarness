codeharness_add_object_module(codeharness_plugins_objects
    plugin_loader.cpp
)

target_link_libraries(codeharness_plugins_objects
    PRIVATE
        codeharness_foundation
        codeharness_hooks
        codeharness_mcp
        codeharness_tools
)

codeharness_link_first_existing(codeharness_plugins_objects PRIVATE fmt fmt::fmt)
codeharness_link_first_existing(codeharness_plugins_objects PRIVATE Glob Glob)
codeharness_link_first_existing(codeharness_plugins_objects PRIVATE nlohmann_json nlohmann_json::nlohmann_json)
