add_executable(path_tracer
    main.cpp
)

target_include_directories(path_tracer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/extern/stb
    ${PROJECT_SOURCE_DIR}/extern/tinyobjloader
    ${PROJECT_SOURCE_DIR}/extern/CLI11/include
)

target_link_libraries(path_tracer PRIVATE
    spdlog::spdlog
    yaml-cpp::yaml-cpp
    Eigen3::Eigen
)

add_custom_command(TARGET path_tracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    $<TARGET_FILE_DIR:path_tracer>/config
)
