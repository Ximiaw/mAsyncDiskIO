cmake_minimum_required(VERSION 3.10)

project(mAsyncDiskIO)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "build test" OFF)

add_subdirectory(src)

if (BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    include(GoogleTest)
    add_executable(io_test test_async_io.cpp)
    target_include_directories(io_test PRIVATE "include")
    target_link_libraries(io_test PRIVATE GTest::gtest_main mAsyncDiskIO)
    gtest_discover_tests(io_test)
endif()