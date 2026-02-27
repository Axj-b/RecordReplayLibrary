cmake_minimum_required(VERSION 3.16)

add_executable(replay_ffplay main.cpp)

target_link_libraries(replay_ffplay PRIVATE recplay)

target_compile_features(replay_ffplay PRIVATE cxx_std_17)

set_target_properties(replay_ffplay PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
