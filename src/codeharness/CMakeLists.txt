find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(expected-lite CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)
find_package(reproc++ CONFIG REQUIRED)
find_package(Glob CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS beast)
find_package(OpenSSL CONFIG REQUIRED)

add_library(codeharness
  Host/HostPath.cpp
  Host/LocalHost.cpp
  Host/Environment.cpp
  Host/CurrentHost.cpp
  Llm/SseParser.cpp
  Llm/MessageJson.cpp
  Llm/OpenAiProvider.cpp
  Llm/BeastHttpClient.cpp
  Llm/Capability.cpp
  Engine/Loop.cpp
  Tools/ToolOutput.cpp
  Tools/ToolManager.cpp
  Tools/ReadFile.cpp
  Tools/WriteFile.cpp
  Tools/EditFile.cpp
  Tools/Glob.cpp
  Tools/Grep.cpp
  Tools/Bash.cpp
)

target_include_directories(codeharness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(codeharness PUBLIC
  reproc++
  fmt::fmt
  spdlog::spdlog
  nonstd::expected-lite
  re2::re2
  Glob
  absl::status
  absl::statusor
  nlohmann_json::nlohmann_json
  Boost::beast
  OpenSSL::SSL
)

if(CODEHARNESS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(${CMAKE_SOURCE_DIR}/tests ${CMAKE_BINARY_DIR}/tests)
endif()