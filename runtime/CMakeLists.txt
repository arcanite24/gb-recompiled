# runtime/CMakeLists.txt
# GameBoy Runtime Library

add_library(gbrt STATIC
    src/gbrt.c
    src/ppu.c
    src/platform_sdl.c
)

target_include_directories(gbrt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(gbrt PUBLIC c_std_11)

# Debug mode option
option(GB_DEBUG "Enable debug logging" OFF)
option(GB_DEBUG_VRAM "Enable VRAM debug logging" OFF)
option(GB_DEBUG_FRAME "Enable frame debug logging" OFF)
option(GB_DEBUG_REGS "Enable register debug logging" OFF)
option(GB_DEBUG_PPU "Enable PPU debug logging" OFF)

if(GB_DEBUG)
    target_compile_definitions(gbrt PUBLIC GB_DEBUG)
    message(STATUS "GameBoy debug logging enabled")
endif()

if(GB_DEBUG_VRAM)
    target_compile_definitions(gbrt PUBLIC GB_DEBUG_VRAM)
endif()
if(GB_DEBUG_FRAME)
    target_compile_definitions(gbrt PUBLIC GB_DEBUG_FRAME)
endif()
if(GB_DEBUG_REGS)
    target_compile_definitions(gbrt PUBLIC GB_DEBUG_REGS)
endif()
if(GB_DEBUG_PPU)
    target_compile_definitions(gbrt PUBLIC GB_DEBUG_PPU)
endif()

# Platform-specific settings
if(APPLE)
    target_compile_definitions(gbrt PRIVATE GB_PLATFORM_MACOS)
elseif(WIN32)
    target_compile_definitions(gbrt PRIVATE GB_PLATFORM_WINDOWS)
else()
    target_compile_definitions(gbrt PRIVATE GB_PLATFORM_LINUX)
endif()

# SDL2 for platform layer (optional)
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    target_link_libraries(gbrt PUBLIC SDL2::SDL2)
    target_compile_definitions(gbrt PUBLIC GB_HAS_SDL2)
endif()
