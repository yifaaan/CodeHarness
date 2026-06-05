codeharness_add_object_module(codeharness_skills_objects
    bundled_skills.cpp
    skill_loader.cpp
    skill_registry.cpp
)

target_link_libraries(codeharness_skills_objects
    PRIVATE
        codeharness_foundation
        codeharness_tools
)

codeharness_link_first_existing(codeharness_skills_objects PRIVATE fmt fmt::fmt)
codeharness_link_first_existing(codeharness_skills_objects PRIVATE Glob Glob)
target_link_libraries(codeharness_skills_objects PRIVATE spdlog::spdlog)
codeharness_link_first_existing(codeharness_skills_objects PRIVATE yaml-cpp yaml-cpp::yaml-cpp)
