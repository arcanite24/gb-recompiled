# GameBoy Recompiled - Static Recompiler for Game Boy ROMs
# Top-level CMake configuration

cmake_minimum_required(VERSION 3.20)
project(gameboy-recompiled 
    VERSION 0.1.0
    DESCRIPTION "Static recompiler for Game Boy (DMG/CGB) ROMs"
    LANGUAGES C CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C Standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O3)
    endif()
elseif(MSVC)
    add_compile_options(/W4)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Find SDL2 (for runtime)
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    message(STATUS "Found SDL2: ${SDL2_INCLUDE_DIRS}")
else()
    message(STATUS "SDL2 not found - runtime will not be built")
endif()

# Subdirectories
add_subdirectory(recompiler)

if(SDL2_FOUND)
    add_subdirectory(runtime)
endif()

# Optional: Build tests
option(BUILD_TESTS "Build test suite" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install rules
install(TARGETS gbrecomp
    RUNTIME DESTINATION bin
)

# Summary
message(STATUS "")
message(STATUS "GameBoy Recompiled Configuration:")
message(STATUS "  Version:      ${PROJECT_VERSION}")
message(STATUS "  Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  SDL2:         ${SDL2_FOUND}")
message(STATUS "  Tests:        ${BUILD_TESTS}")
message(STATUS "")
