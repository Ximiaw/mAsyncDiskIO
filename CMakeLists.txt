cmake_minimum_required(VERSION 3.10)

project(mAsyncDiskIO)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(src)

add_executable(main main.cpp)
target_link_libraries(main mAsyncDiskIOLib)