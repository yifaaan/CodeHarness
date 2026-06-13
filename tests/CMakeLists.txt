find_package(doctest CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(reproc++ CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)

add_executable(codeharness_tests
  host/main.cpp
  host/local_host_test.cpp
  host/host_path_test.cpp
  host/environment_test.cpp
  host/process_lifecycle_test.cpp
  llm/sse_parser_test.cpp
  llm/message_json_test.cpp
  llm/openai_provider_test.cpp
  llm/capability_test.cpp
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