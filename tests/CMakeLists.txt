find_package(doctest CONFIG REQUIRED)

add_executable(fake_mcp_server fake_mcp_server.cpp)
target_link_libraries(fake_mcp_server PRIVATE codeharness_foundation)

add_executable(codeharness_tests
    main.cpp
    command_tests.cpp
    config_tests.cpp
    coordinator_tests.cpp
    core_tests.cpp
    engine_permission_tests.cpp
    engine_tests.cpp
    hook_tests.cpp
    mailbox_tests.cpp
    mcp_tests.cpp
    memory_tests.cpp
    permission_tests.cpp
    plugin_tests.cpp
    prompt_tests.cpp
    provider_tests.cpp
    runtime_tests.cpp
    skill_tests.cpp
    task_tests.cpp
    team_lifecycle_tests.cpp
    test_support.cpp
    tool_tests.cpp
    ui_backend_tests.cpp
    version_tests.cpp
)
target_link_libraries(codeharness_tests PRIVATE codeharness_core codeharness_network_objects doctest::doctest)
target_compile_definitions(codeharness_tests PRIVATE "CODEHARNESS_FAKE_MCP_SERVER=\"$<TARGET_FILE:fake_mcp_server>\"")
target_compile_definitions(codeharness_tests PRIVATE "CODEHARNESS_CMAKE_COMMAND=\"${CMAKE_COMMAND}\"")
target_compile_definitions(codeharness_tests PRIVATE "CODEHARNESS_EXE=\"$<TARGET_FILE:codeharness>\"")

add_test(NAME codeharness_tests COMMAND codeharness_tests)
