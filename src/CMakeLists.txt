set(RECPLAY_SOURCES
    version.cpp
    session.cpp
    writer.cpp
    reader.cpp
    splitter.cpp
    detail/crc32.cpp
    detail/chunk_accumulator.cpp
    detail/index_builder.cpp
    detail/manifest_io.cpp
    detail/file_map.cpp
)

add_library(recplay ${RECPLAY_SOURCES})
add_library(recplay::recplay ALIAS recplay)

# ---------------------------------------------------------------------------
# Include paths
# ---------------------------------------------------------------------------
target_include_directories(recplay
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}   # src/ — for detail/ includes
)

# ---------------------------------------------------------------------------
# Compiler options
# ---------------------------------------------------------------------------
target_compile_options(recplay PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /wd4251>
    # GCC/Clang: strict warnings + hide all symbols by default (only RECPLAY_API exported)
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Werror
                                      -fvisibility=hidden -fvisibility-inlines-hidden>
)

# ---------------------------------------------------------------------------
# Platform-specific compile definitions
# ---------------------------------------------------------------------------
target_compile_definitions(recplay
    PRIVATE
        # Tell library source it is being compiled (enables __declspec(dllexport) on Windows)
        RECPLAY_BUILDING_LIBRARY
        # Windows: suppress common macro / CRT-deprecation noise
        $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS>
    INTERFACE
        # Propagate to consumers so they get __declspec(dllimport) when using the DLL
        $<$<BOOL:${BUILD_SHARED_LIBS}>:RECPLAY_SHARED_LIBRARY>
)

# ---------------------------------------------------------------------------
# Compile-time feature definitions
# ---------------------------------------------------------------------------
if(RECPLAY_ENABLE_CRC)
    target_compile_definitions(recplay PUBLIC RECPLAY_CRC_ENABLED)
endif()

if(RECPLAY_ENABLE_LZ4 AND (lz4_FOUND OR LZ4_FOUND))
    target_compile_definitions(recplay PUBLIC RECPLAY_HAS_LZ4)
    if(lz4_FOUND)
        target_link_libraries(recplay PRIVATE lz4::lz4)
    else()
        target_include_directories(recplay PRIVATE ${LZ4_INCLUDE_DIRS})
        target_link_libraries(recplay PRIVATE ${LZ4_LIBRARIES})
    endif()
endif()

if(RECPLAY_ENABLE_ZSTD AND (zstd_FOUND OR ZSTD_FOUND))
    target_compile_definitions(recplay PUBLIC RECPLAY_HAS_ZSTD)
    if(zstd_FOUND)
        target_link_libraries(recplay PRIVATE zstd::libzstd_static)
    else()
        target_include_directories(recplay PRIVATE ${ZSTD_INCLUDE_DIRS})
        target_link_libraries(recplay PRIVATE ${ZSTD_LIBRARIES})
    endif()
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
if(RECPLAY_INSTALL)
    include(GNUInstallDirs)

    install(TARGETS recplay
        EXPORT       recplayTargets
        ARCHIVE      DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY      DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME      DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# ---------------------------------------------------------------------------
# std::filesystem link (GCC < 9 needs explicit -lstdc++fs)
# ---------------------------------------------------------------------------
target_link_libraries(recplay PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
)
