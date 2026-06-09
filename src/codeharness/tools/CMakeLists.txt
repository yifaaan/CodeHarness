codeharness_add_static_module(codeharness_tools
    ask_user_tool.cpp
    bash_tool.cpp
    edit_file_tool.cpp
    glob_tool.cpp
    grep_tool.cpp
    permission_target.cpp
    read_file_tool.cpp
    sleep_tool.cpp
    text_file.cpp
    todo_write_tool.cpp
    tool.cpp
    tool_registry.cpp
    web_fetch_tool.cpp
    web_search_tool.cpp
    workspace_path.cpp
    write_file_tool.cpp
)

target_link_libraries(codeharness_tools PUBLIC codeharness_foundation)
target_link_libraries(codeharness_tools PRIVATE codeharness_network_objects)

codeharness_link_first_existing(codeharness_tools PUBLIC nlohmann_json nlohmann_json::nlohmann_json)
codeharness_link_first_existing(codeharness_tools PRIVATE fmt fmt::fmt)
codeharness_link_first_existing(codeharness_tools PRIVATE Glob Glob)
codeharness_link_first_existing(codeharness_tools PRIVATE re2 re2::re2)
codeharness_link_first_existing(codeharness_tools PRIVATE reproc++ reproc++ reproc::reproc++)
target_link_libraries(codeharness_tools PRIVATE spdlog::spdlog)

add_library(codeharness::tools ALIAS codeharness_tools)
