add_executable(joint_bilateral
    joint_bilateral.cpp
)

target_include_directories(joint_bilateral PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/extern/stb
    ${PROJECT_SOURCE_DIR}/extern/CLI11/include
)

target_link_libraries(joint_bilateral PRIVATE
    yaml-cpp::yaml-cpp
    Eigen3::Eigen
)

add_executable(atrous_wavelet
    atrous_wavelet.cpp
)

target_include_directories(atrous_wavelet PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/extern/stb
    ${PROJECT_SOURCE_DIR}/extern/CLI11/include
)

target_link_libraries(atrous_wavelet PRIVATE
    yaml-cpp::yaml-cpp
    Eigen3::Eigen
)
