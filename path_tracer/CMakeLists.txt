find_package(OpenMP QUIET)

add_executable(path_tracer
    main.cpp
)

target_include_directories(path_tracer PRIVATE
    ${PROJECT_SOURCE_DIR}
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

if(OpenMP_CXX_FOUND)
    target_link_libraries(path_tracer PRIVATE OpenMP::OpenMP_CXX)
endif()

add_executable(stats_test tests/stats_test.cpp)
target_include_directories(stats_test PRIVATE ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(stats_test PRIVATE Eigen3::Eigen)
add_test(NAME stats_test COMMAND stats_test)

add_executable(config_test tests/config_test.cpp)
target_include_directories(config_test PRIVATE ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(config_test PRIVATE yaml-cpp::yaml-cpp Eigen3::Eigen)
add_test(NAME config_test COMMAND config_test ${CMAKE_CURRENT_SOURCE_DIR}/config)

add_custom_command(TARGET path_tracer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    $<TARGET_FILE_DIR:path_tracer>/config
)
