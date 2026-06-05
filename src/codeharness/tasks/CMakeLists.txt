codeharness_add_static_module(codeharness_tasks
    task_manager.cpp
    task_tools.cpp
)

target_link_libraries(codeharness_tasks
    PUBLIC
        codeharness_foundation
    PRIVATE
        codeharness_tools
)

codeharness_link_first_existing(codeharness_tasks PRIVATE date date::date)
codeharness_link_first_existing(codeharness_tasks PRIVATE fmt fmt::fmt)
codeharness_link_first_existing(codeharness_tasks PRIVATE nlohmann_json nlohmann_json::nlohmann_json)
codeharness_link_first_existing(codeharness_tasks PRIVATE reproc++ reproc++ reproc::reproc++)
target_link_libraries(codeharness_tasks PRIVATE spdlog::spdlog)

add_library(codeharness::tasks ALIAS codeharness_tasks)
