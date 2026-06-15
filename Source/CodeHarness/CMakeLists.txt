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
find_package(tomlplusplus CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

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
  Config/ConfigManager.cpp
  Config/ProviderManager.cpp
  Agent/Agent.cpp
  Engine/Loop.cpp
  Tools/ToolOutput.cpp
  Tools/ToolManager.cpp
  Tools/ReadFile.cpp
  Tools/WriteFile.cpp
  Tools/EditFile.cpp
  Tools/Glob.cpp
  Tools/Grep.cpp
  Tools/Bash.cpp
  Records/RecordJson.cpp
  Records/FilePersistence.cpp
  Records/AgentRecords.cpp
  Permission/PermissionGate.cpp
  Session/WorkdirKey.cpp
  Session/SessionStore.cpp
  Session/Session.cpp
  Cli/CliParser.cpp
  Cli/RunPrompt.cpp
  Context/TokenEstimate.cpp
  Context/ContextMemory.cpp
  Context/Compactor.cpp
  Hooks/HookEngine.cpp
  Skills/SkillTypes.cpp
  Skills/SkillParser.cpp
  Skills/SkillScanner.cpp
  Skills/SkillRegistry.cpp
  Skills/SkillManager.cpp
  Skills/SkillTool.cpp
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
  tomlplusplus::tomlplusplus
  CLI11::CLI11
  yaml-cpp::yaml-cpp
)

if(CODEHARNESS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(${CMAKE_SOURCE_DIR}/Test ${CMAKE_BINARY_DIR}/Test)
endif()

# Non-interactive CLI executable. Always built (a default build should produce a
# runnable program). The TUI/shell mode is deferred; this is one-shot --prompt.
# CliParser.cpp and RunPrompt.cpp live in the codeharness library (so tests can
# link them); only Main.cpp is exclusive to the executable.
add_executable(codeharness_cli
  Cli/Main.cpp
)
target_link_libraries(codeharness_cli PRIVATE codeharness)
