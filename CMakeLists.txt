cmake_minimum_required(VERSION 3.25)

project(CodeHarness VERSION 0.1.0 LANGUAGES CXX)

option(CODEHARNESS_BUILD_TESTS "Build CodeHarness tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(codeharness_link_first_existing target visibility package_name)
    foreach(candidate IN LISTS ARGN)
        if(TARGET "${candidate}")
            target_link_libraries("${target}" "${visibility}" "${candidate}")
            return()
        endif()
    endforeach()

    message(FATAL_ERROR "No supported CMake target found for ${package_name}. Tried: ${ARGN}")
endfunction()

function(codeharness_add_static_module target)
    add_library("${target}" STATIC)
    target_sources("${target}" PRIVATE ${ARGN})
    target_link_libraries("${target}" PUBLIC codeharness_options)
endfunction()

function(codeharness_add_object_module target)
    add_library("${target}" OBJECT)
    target_sources("${target}" PRIVATE ${ARGN})
    target_link_libraries("${target}" PUBLIC codeharness_options)
endfunction()

find_package(CLI11 CONFIG REQUIRED)
find_package(date CONFIG REQUIRED)
find_package(expected-lite CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Glob CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS beast url)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)
find_package(reproc++ CONFIG REQUIRED)
find_package(simdutf CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(libgit2 CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)

add_library(codeharness_options INTERFACE)
target_include_directories(codeharness_options INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_compile_features(codeharness_options INTERFACE cxx_std_20)
target_compile_definitions(codeharness_options INTERFACE
    "CODEHARNESS_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\""
    $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0A00>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
)

add_subdirectory(src)

include(CTest)

if(CODEHARNESS_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()
