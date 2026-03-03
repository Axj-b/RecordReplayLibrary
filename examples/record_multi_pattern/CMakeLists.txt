cmake_minimum_required(VERSION 3.16)

add_executable(record_multi_pattern main.cpp)

target_link_libraries(record_multi_pattern PRIVATE recplay)

target_compile_features(record_multi_pattern PRIVATE cxx_std_17)

set_target_properties(record_multi_pattern PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
