cmake_minimum_required(VERSION 3.25)

project(CodeHarness VERSION 0.1.0 LANGUAGES CXX)

option(CODEHARNESS_BUILD_TESTS "Build CodeHarness tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


add_subdirectory(src)
