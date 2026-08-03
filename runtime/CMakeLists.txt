# runtime/CMakeLists.txt
# GameBoy Runtime Library

add_library(gbrt STATIC
    src/gbrt.c
    src/gbrt_data_mod.c
    src/gbrt_hash.c
    src/gbrt_host_configuration.c
    src/gbrt_port.c
    src/gbrt_presentation.c
    src/gbrt_semantic.c
    src/differential.c
    src/interpreter.c
    src/ppu.c
    src/audio.c
    src/audio_stats.c
    src/platform_sdl.cpp
)

option(GBRECOMP_ENABLE_NATIVE_PATCHES
    "Compile the native replacement SDK runtime" ${BUILD_TESTS})
if(GBRECOMP_ENABLE_NATIVE_PATCHES)
    target_sources(gbrt PRIVATE
        src/gbrt_gameplay_mutation.c
        src/gbrt_native_patch.c
    )
    target_compile_definitions(gbrt PUBLIC GBRT_ENABLE_NATIVE_PATCHES)
endif()

# Dear ImGui sources (if using SDL2)
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    target_sources(gbrt PRIVATE
        vendor/imgui/imgui.cpp
        vendor/imgui/imgui_draw.cpp
        vendor/imgui/imgui_tables.cpp
        vendor/imgui/imgui_widgets.cpp
        vendor/imgui/backends/imgui_impl_sdl2.cpp
        vendor/imgui/backends/imgui_impl_sdlrenderer2.cpp
    )
    target_include_directories(gbrt PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
    )
endif()

target_include_directories(gbrt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(gbrt PUBLIC c_std_11 cxx_std_17)

option(GBRECOMP_ENABLE_PERFORMANCE_COUNTERS
    "Compile low-overhead generated/runtime performance counters" OFF)
if(GBRECOMP_ENABLE_PERFORMANCE_COUNTERS)
    target_compile_definitions(gbrt PUBLIC GBRT_ENABLE_PERFORMANCE_COUNTERS)
endif()

if(BUILD_TESTS AND SDL2_FOUND)
    find_package(Threads REQUIRED)
    target_link_libraries(gbrt PRIVATE Threads::Threads)
    target_compile_definitions(gbrt PUBLIC GBRT_ENABLE_TEST_HOOKS)
endif()

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
if(SDL2_FOUND)
    target_link_libraries(gbrt PUBLIC SDL2::SDL2)
    target_compile_definitions(gbrt PUBLIC GB_HAS_SDL2)
endif()
