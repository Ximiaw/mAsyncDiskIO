cmake_minimum_required(VERSION 3.10)

file(GLOB_RECURSE libsrc "*.cpp")

add_library(${PROJECT_NAME} ${libsrc})

target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_link_libraries(${PROJECT_NAME} PUBLIC uring)