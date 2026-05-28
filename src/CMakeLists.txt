
file(GLOB_RECURSE libsrc "*.cpp")

add_library(${PROJECT_NAME} ${libsrc})

target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_SOURCE_DIR}/include
)