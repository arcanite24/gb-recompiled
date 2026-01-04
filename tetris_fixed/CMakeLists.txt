cmake_minimum_required(VERSION 3.16)
project(tetris C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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

# Main executable
add_executable(tetris
    tetris_main.c
    tetris.c
    tetris_rom.c
)

target_link_libraries(tetris gbrt)
