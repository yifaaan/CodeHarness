add_subdirectory(codeharness)

add_executable(codeharness main.cpp)
target_link_libraries(codeharness PRIVATE codeharness_cli)
