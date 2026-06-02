cmake_minimum_required(VERSION 3.10)

project(mAsyncDiskIO)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "build test" OFF)

add_subdirectory(src)

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory("test")
endif()