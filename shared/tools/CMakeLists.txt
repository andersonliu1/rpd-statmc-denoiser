cmake_minimum_required(VERSION 3.20)

set(TOOLS_SOURCES
    compare_hdr.cpp
    compare_png.cpp
    hdr_metrics.cpp
    eval_tools.cpp
    tonemap_hdr.cpp
)

foreach(tool_src IN LISTS TOOLS_SOURCES)
    get_filename_component(tool_name "${tool_src}" NAME_WE)
    add_executable(${tool_name} ${tool_src})
    target_include_directories(${tool_name} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/extern/eigen
        ${CMAKE_SOURCE_DIR}/extern/stb
    )
    target_compile_features(${tool_name} PRIVATE cxx_std_20)
endforeach()
