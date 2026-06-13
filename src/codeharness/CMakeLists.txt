find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(expected-lite CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)
find_package(reproc++ CONFIG REQUIRED)
find_package(Glob CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)

add_library(codeharness
  host/host_path.cpp
  host/local_host.cpp
  host/environment.cpp
  host/current_host.cpp
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
)

if(CODEHARNESS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(${CMAKE_SOURCE_DIR}/tests ${CMAKE_BINARY_DIR}/tests)
endif()