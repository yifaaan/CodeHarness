find_package(doctest CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(reproc++ CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)

add_executable(codeharness_tests
  Host/Main.cpp
  Host/LocalHostTest.cpp
  Host/HostPathTest.cpp
  Host/EnvironmentTest.cpp
  Host/ProcessLifecycleTest.cpp
  Llm/SseParserTest.cpp
  Llm/MessageJsonTest.cpp
  Llm/OpenAiProviderTest.cpp
  Llm/CapabilityTest.cpp
  Config/ConfigManagerTest.cpp
  Config/ProviderManagerTest.cpp
  Agent/AgentTest.cpp
  Engine/LoopTest.cpp
  Tools/ToolOutputTest.cpp
  Tools/ToolManagerTest.cpp
  Tools/FileToolsTest.cpp
  Tools/SearchToolsTest.cpp
  Tools/BashTest.cpp
  Records/RecordsTest.cpp
  Permission/PermissionTest.cpp
  Session/SessionTest.cpp
  Cli/CliTest.cpp
  Context/ContextTest.cpp
  Hooks/HooksTest.cpp
)

target_link_libraries(codeharness_tests PRIVATE
  codeharness
  doctest::doctest
  reproc++
  fmt::fmt
  absl::status
  absl::statusor
)

target_include_directories(codeharness_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME codeharness_tests COMMAND codeharness_tests)
