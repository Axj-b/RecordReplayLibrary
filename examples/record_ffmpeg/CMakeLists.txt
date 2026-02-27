cmake_minimum_required(VERSION 3.16)

add_executable(record_ffmpeg main.cpp)

target_link_libraries(record_ffmpeg PRIVATE recplay)

target_compile_features(record_ffmpeg PRIVATE cxx_std_17)

set_target_properties(record_ffmpeg PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
