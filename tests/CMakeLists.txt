find_package(doctest CONFIG REQUIRED)

add_executable(codeharness_tests main.cpp)
target_link_libraries(codeharness_tests PRIVATE codeharness_core doctest::doctest)

add_test(NAME codeharness_tests COMMAND codeharness_tests)
