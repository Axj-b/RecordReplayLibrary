find_package(GTest QUIET)

if(NOT GTest_FOUND)
    # Download and build GoogleTest automatically if not found on the system
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

# ---------------------------------------------------------------------------
# Helper macro — adds a test executable and registers it with CTest
# ---------------------------------------------------------------------------
macro(recplay_add_test TEST_NAME)
    add_executable(${TEST_NAME} ${ARGN})
    target_link_libraries(${TEST_NAME} PRIVATE recplay::recplay GTest::gtest_main)
    target_compile_options(${TEST_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
    )
    gtest_discover_tests(${TEST_NAME})
endmacro()

# ---------------------------------------------------------------------------
# Test executables
# ---------------------------------------------------------------------------
recplay_add_test(test_format     test_format.cpp)
recplay_add_test(test_channel    test_channel.cpp)
recplay_add_test(test_session    test_session.cpp)
recplay_add_test(test_writer     test_writer.cpp)
recplay_add_test(test_reader     test_reader.cpp)
recplay_add_test(test_splitter   test_splitter.cpp)
recplay_add_test(test_roundtrip  test_roundtrip.cpp)
# file_map test needs access to the private src/detail/ headers
recplay_add_test(test_file_map   test_file_map.cpp)
target_include_directories(test_file_map PRIVATE ${CMAKE_SOURCE_DIR}/src)
