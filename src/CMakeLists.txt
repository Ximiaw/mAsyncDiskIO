cmake_minimum_required(VERSION 3.10)

project(mAsyncDiskIOLib)

file(GLOB_RECURSE libsrc "*.cpp")

add_library(${PROJECT_NAME} ${libsrc})

target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(mAsyncDiskIOLib PRIVATE uring)