find_package(doctest CONFIG REQUIRED)

add_executable(fake_mcp_server fake_mcp_server.cpp)
target_link_libraries(fake_mcp_server PRIVATE codeharness_foundation)

add_executable(codeharness_tests main.cpp)
target_link_libraries(codeharness_tests PRIVATE codeharness_core doctest::doctest)
target_compile_definitions(codeharness_tests PRIVATE "CODEHARNESS_FAKE_MCP_SERVER=\"$<TARGET_FILE:fake_mcp_server>\"")

add_test(NAME codeharness_tests COMMAND codeharness_tests)
