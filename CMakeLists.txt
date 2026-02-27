cmake_minimum_required(VERSION 3.16...4.0)

project(recplay
    VERSION      1.0.0
    DESCRIPTION  "Self-contained C++ sensor data recording and replay library"
    LANGUAGES    CXX
)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(RECPLAY_BUILD_TESTS    "Build unit tests"                    ON)
option(RECPLAY_BUILD_TOOLS    "Build command-line tools (CLI)"      ON)
option(RECPLAY_ENABLE_LZ4     "Enable LZ4 compression support"      ON)
option(RECPLAY_ENABLE_ZSTD    "Enable Zstd compression support"     ON)
option(RECPLAY_ENABLE_CRC     "Enable CRC32 checksums by default"   ON)
option(RECPLAY_INSTALL        "Generate install targets"            ON)

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
if(RECPLAY_ENABLE_LZ4)
    find_package(lz4 QUIET)
    if(NOT lz4_FOUND)
        # Try pkg-config fallback
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LZ4 QUIET liblz4)
        endif()
    endif()
    if(lz4_FOUND OR LZ4_FOUND)
        message(STATUS "recplay: LZ4 found — compression enabled")
    else()
        message(WARNING "recplay: LZ4 not found — LZ4 compression will be unavailable at runtime")
    endif()
endif()

if(RECPLAY_ENABLE_ZSTD)
    find_package(zstd QUIET)
    if(NOT zstd_FOUND)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(ZSTD QUIET libzstd)
        endif()
    endif()
    if(zstd_FOUND OR ZSTD_FOUND)
        message(STATUS "recplay: Zstd found — compression enabled")
    else()
        message(WARNING "recplay: Zstd not found — Zstd compression will be unavailable at runtime")
    endif()
endif()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(RECPLAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install / packaging
# ---------------------------------------------------------------------------
if(RECPLAY_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(DIRECTORY include/recplay
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/recplayConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/recplayConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/recplayConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/recplay
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/recplayConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/recplayConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/recplay
    )

    install(EXPORT recplayTargets
        FILE        recplayTargets.cmake
        NAMESPACE   recplay::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/recplay
    )
endif()
