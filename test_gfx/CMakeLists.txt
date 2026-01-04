cmake_minimum_required(VERSION 3.16)
project(gfxtest C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Debug options
option(GB_DEBUG "Enable debug logging" OFF)

# Runtime library path (relative to this output directory)
set(GBRT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../runtime")

# Find SDL2
find_package(SDL2 REQUIRED)

# Create runtime library with PPU and platform support
add_library(gbrt STATIC
    ${GBRT_DIR}/src/gbrt.c
    ${GBRT_DIR}/src/ppu.c
    ${GBRT_DIR}/src/platform_sdl.c
)
target_include_directories(gbrt PUBLIC ${GBRT_DIR}/include)
target_link_libraries(gbrt PUBLIC SDL2::SDL2)
target_compile_definitions(gbrt PUBLIC GB_HAS_SDL2)

if(GB_DEBUG)
    target_compile_definitions(gbrt PUBLIC GB_DEBUG)
    message(STATUS "Debug logging enabled for gfxtest")
endif()

# Main executable
add_executable(gfxtest
    gfxtest_main.c
    gfxtest.c
    gfxtest_rom.c
)

target_link_libraries(gfxtest gbrt)
