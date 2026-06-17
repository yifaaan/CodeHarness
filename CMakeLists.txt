cmake_minimum_required(VERSION 3.25)

project(CodeHarness VERSION 0.1.0 LANGUAGES CXX)

option(CODEHARNESS_BUILD_TESTS "Build CodeHarness tests" ON)
if(WIN32)
  option(CODEHARNESS_BUILD_DESKTOP_WINUI "Build the CodeHarness WinUI desktop app" ON)
else()
  option(CODEHARNESS_BUILD_DESKTOP_WINUI "Build the CodeHarness WinUI desktop app" OFF)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CODEHARNESS_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(Source)

if(CODEHARNESS_BUILD_DESKTOP_WINUI)
  if(NOT WIN32)
    message(FATAL_ERROR "CODEHARNESS_BUILD_DESKTOP_WINUI is only supported on Windows.")
  endif()
  add_subdirectory(Apps/DesktopWinUI)
endif()
