cmake_minimum_required(VERSION 3.22)
project(fserve VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FSERVE_WITH_LLAMA "Build the real llama.cpp engine (Phase 5)" OFF)
option(FSERVE_BUILD_TESTS "Build the GoogleTest suite" ON)

# ---------------------------------------------------------------------------
# Keep Windows toolchains out of this Linux build.
#
# Development happens on Windows with the source on /mnt/d and the build under
# WSL, so the inherited PATH contains Windows directories such as
# C:\msys64\ucrt64\bin. CMake derives find_package search prefixes from PATH,
# which lets a dependency pick up MinGW headers and pass
# -isystem /mnt/c/msys64/ucrt64/include to g++-12. That fails with
# "#error Only Win32 target is supported!".
#
# Nothing here needs PATH-based discovery: every dependency comes from
# FetchContent and Threads comes from the compiler. This matters more in
# Phase 5, where llama.cpp probes for BLAS and CURL.
# ---------------------------------------------------------------------------
set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH OFF)

include(cmake/Dependencies.cmake)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# fserve_core -- everything worth testing.
#
# The executable is a thin shell over this library, and the test binary links
# the same library. Logic that lives in main() cannot be tested without
# spawning a process, so main() holds none.
# ---------------------------------------------------------------------------
add_library(fserve_core
    src/engine/mock_engine.cpp
)

target_include_directories(fserve_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(fserve_core
    PUBLIC Threads::Threads
)

target_compile_options(fserve_core PRIVATE -Wall -Wextra -Wpedantic)

if(FSERVE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
