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

find_package(CLI11 CONFIG REQUIRED)
find_package(date CONFIG REQUIRED)
find_package(expected-lite CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Glob CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)
find_package(reproc++ CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(libgit2 CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)

file(GLOB_RECURSE CODEHARNESS_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/codeharness/*.cpp"
)

add_library(codeharness_core STATIC ${CODEHARNESS_CORE_SOURCES})
target_include_directories(codeharness_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(codeharness_core PUBLIC cxx_std_20)
target_compile_definitions(codeharness_core PUBLIC "CODEHARNESS_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

codeharness_link_first_existing(codeharness_core PUBLIC CLI11 CLI11::CLI11)
codeharness_link_first_existing(codeharness_core PUBLIC date date::date)
codeharness_link_first_existing(codeharness_core PUBLIC expected-lite nonstd::expected-lite)
codeharness_link_first_existing(codeharness_core PUBLIC fmt fmt::fmt)
codeharness_link_first_existing(codeharness_core PUBLIC Glob Glob)
codeharness_link_first_existing(codeharness_core PUBLIC nlohmann_json nlohmann_json::nlohmann_json)
codeharness_link_first_existing(codeharness_core PUBLIC re2 re2::re2)
codeharness_link_first_existing(codeharness_core PUBLIC reproc++ reproc++ reproc::reproc++)
codeharness_link_first_existing(codeharness_core PUBLIC spdlog spdlog::spdlog)
codeharness_link_first_existing(codeharness_core PUBLIC yaml-cpp yaml-cpp::yaml-cpp)
codeharness_link_first_existing(codeharness_core PUBLIC libgit2 libgit2::libgit2package libgit2::libgit2)
codeharness_link_first_existing(codeharness_core PUBLIC llhttp llhttp::llhttp llhttp::llhttp_static)

add_executable(codeharness src/main.cpp)
target_link_libraries(codeharness PRIVATE codeharness_core)

include(CTest)

if(CODEHARNESS_BUILD_TESTS AND BUILD_TESTING)
    find_package(doctest CONFIG REQUIRED)

    file(GLOB_RECURSE CODEHARNESS_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )

    add_executable(codeharness_tests ${CODEHARNESS_TEST_SOURCES})
    target_link_libraries(codeharness_tests PRIVATE codeharness_core doctest::doctest)

    add_test(NAME codeharness_tests COMMAND codeharness_tests)
endif()
