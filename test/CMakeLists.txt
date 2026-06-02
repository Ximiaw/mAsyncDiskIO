cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest REQUIRED)
include(GoogleTest)

file(GLOB_RECURSE test_src CONFIGURE_DEPENDS "*.cpp")

foreach(src ${test_src})
    get_filename_component(filename ${src} NAME_WE)
    add_executable(${filename} ${src})
    
    target_compile_options(${filename} PRIVATE 
        $<$<CONFIG:DEBUG>:-O0 -g -fsanitize=address -fno-omit-frame-pointer>
        $<$<CONFIG:RELEASE>:-O2 -DNDEBUG>
    )
    target_link_options(${filename} PRIVATE 
        $<$<CONFIG:DEBUG>:-fsanitize=address>
    )
    
    target_include_directories(${filename} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
    target_link_libraries(${filename} PRIVATE 
        GTest::gtest_main 
        ${PROJECT_NAME}
        pthread
    )
    
    gtest_discover_tests(${filename})
endforeach()