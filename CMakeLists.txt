# Copyright (c) 2018 Harikrishnan (harikrishnan.prabakaran@gmail.com) Distributed under the MIT
# License. See accompanying file LICENSE.md or copy at http://opensource.org/licenses/MIT

cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PROJECT_NAME sync_primitives)

project(${PROJECT_NAME} C CXX)
set(PROJECT_PATH ${CMAKE_CURRENT_SOURCE_DIR})
set(${CMAKE_PROJECT_NAME}_VERSION_MAJOR 0)
set(${CMAKE_PROJECT_NAME}_VERSION_MINOR 0)
set(${CMAKE_PROJECT_NAME}_VERSION_PATCH 1)
set(
    PROJECT_VERSION
    "${${CMAKE_PROJECT_NAME}_VERSION_MAJOR}.${${CMAKE_PROJECT_NAME}_VERSION_MINOR}.${${CMAKE_PROJECT_NAME}_VERSION_PATCH}"
    )

set(LIB "${PROJECT_NAME}")
set(TEST "test_${PROJECT_NAME}")
set(BENCH "bench_${PROJECT_NAME}")
set(BENCH2 "bench2_${PROJECT_NAME}")

set(SRC_PATH "${PROJECT_PATH}/src")
set(TEST_SRC_PATH "${PROJECT_PATH}/test")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

message(STATUS "Building ${CMAKE_BUILD_TYPE} version!")

include(AdditionalBuildTypes.cmake)
include(ProjectFiles.cmake)
include_directories(AFTER "include")

find_package(Threads)
find_package(Boost REQUIRED COMPONENTS system thread program_options)
find_package(benchmark CONFIG REQUIRED)
find_path(VCPKG_INCLUDE_DIR NAMES gsl catch.hpp)
find_package(folly CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

include_directories(AFTER ${VCPKG_INCLUDE_DIR} ${Boost_INCLUDE_DIRS})
link_libraries(${Boost_LIBRARIES}
               Folly::folly
               Folly::folly_deps
               Folly::follybenchmark
               Folly::folly_test_util
               ZLIB::ZLIB)

if(NOT MSVC)
    add_compile_options("-Wall" "-pedantic")
endif(NOT MSVC)

add_library(${LIB} ${SRC})
target_link_libraries(${LIB} PRIVATE ${CMAKE_THREAD_LIBS_INIT})

add_executable(${BENCH} ${BENCH_SRC})
target_link_libraries(${BENCH} PRIVATE ${LIB} ${CMAKE_THREAD_LIBS_INIT})

add_executable(${BENCH2} ${BENCH2_SRC})
target_link_libraries(${BENCH2}
                      PRIVATE ${LIB}
                              ${CMAKE_THREAD_LIBS_INIT}
                              benchmark::benchmark
                              benchmark::benchmark_main)

add_executable(${TEST} ${TEST_SRC})
target_link_libraries(${TEST} PRIVATE ${LIB} ${CMAKE_THREAD_LIBS_INIT})

enable_testing()
add_test(NAME ${TEST} COMMAND ${TEST})

if(BUILD_COVERAGE_ANALYSIS)
    include(CodeCoverage.cmake)
    message(STATUS "Enabling Code Coverage")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
    set(COVERAGE_EXTRACT
        '${CMAKE_CURRENT_SOURCE_DIR}/include/*'
        '${CMAKE_CURRENT_SOURCE_DIR}/src/*'
        '${CMAKE_CURRENT_SOURCE_DIR}/test/*')
    setup_target_for_coverage(coverage "${TEST}" coverage)
endif(BUILD_COVERAGE_ANALYSIS)
