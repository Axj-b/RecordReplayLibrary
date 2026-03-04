cmake_minimum_required(VERSION 3.16)

add_executable(record_net_streams main.cpp)

target_link_libraries(record_net_streams PRIVATE recplay)

target_compile_features(record_net_streams PRIVATE cxx_std_17)

if(WIN32)
    target_link_libraries(record_net_streams PRIVATE ws2_32)
endif()

set_target_properties(record_net_streams PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
