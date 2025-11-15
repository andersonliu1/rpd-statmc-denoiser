cmake_minimum_required(VERSION 3.20)

project(path_tracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(extern/spdlog)

set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/yaml-cpp)

add_subdirectory(extern/eigen)

add_executable(path_tracer
    main.cpp
)

target_include_directories(path_tracer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/path_tracer
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/tinyobjloader
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/CLI11/include
)

target_compile_definitions(path_tracer PRIVATE
    STB_IMAGE_WRITE_IMPLEMENTATION
    TINYOBJLOADER_IMPLEMENTATION
)

target_link_libraries(path_tracer PRIVATE
    spdlog::spdlog
    yaml-cpp::yaml-cpp
    Eigen3::Eigen
)

add_custom_command(TARGET path_tracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/config
    $<TARGET_FILE_DIR:path_tracer>/config
)
