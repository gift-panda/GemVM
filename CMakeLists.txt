cmake_minimum_required(VERSION 3.31)
project(GemVM C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_BUILD_TYPE Release)

add_compile_definitions(TB_IMPL_V1)

# =========================
# Sources
# =========================
set(GEMVM_SOURCES
        main.c
        common.h
        chunk.c chunk.h
        memory.c memory.h
        debug.c debug.h
        value.c value.h
        vm.c vm.h
        compiler.c compiler.h
        scanner.c scanner.h
        object.c object.h
        table.c table.h
        stringMethods.c
        listMethods.c
        windowMethods.c windowMethods.h
        Math.c Math.h
        linenoise.c linenoise.h
        serialize.c serialize.h
        deserialize.c deserialize.h
        fileMethods.c fileMethods.h
        deserializeBytecode.c deserializeBytecode.h
        deserializeMemory.c deserializeMemory.h
)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# =========================
# Dependencies
# =========================
find_package(PkgConfig REQUIRED)

# SDL (has CMake configs)
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)

# ---- SDL2_gfx (NO pkg-config, NO CMake config) ----
find_library(SDL2_GFX_LIBRARY
        NAMES SDL2_gfx sdl2_gfx
        PATHS /usr/lib /usr/local/lib /usr/lib64
        REQUIRED
)

find_library(TERMBOX2_LIBRARY
    NAMES termbox2 libtermbox2
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/termbox2"
    REQUIRED
)

# Boehm GC
pkg_check_modules(GC REQUIRED bdw-gc)

# =========================
# Executable
# =========================
add_executable(GemVM ${GEMVM_SOURCES})

target_compile_definitions(GemVM PRIVATE GC_THREADS)

# =========================
# Includes
# =========================
target_include_directories(GemVM PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_IMAGE_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
        ${GC_INCLUDE_DIRS}
        ../termbox2
)

# =========================
# Linking (DYNAMIC)
# =========================
target_link_libraries(GemVM
        SDL2::SDL2
        SDL2_image::SDL2_image
        SDL2_ttf::SDL2_ttf
        ${SDL2_GFX_LIBRARY}
        ${TERMBOX2_LIBRARY}
        ${GC_LIBRARIES}
        m png jpeg z pthread freetype
)

# =========================
# RPATH (runtime .so lookup)
# =========================
set_target_properties(GemVM PROPERTIES
        BUILD_RPATH "/usr/local/lib"
        INSTALL_RPATH "/usr/local/lib"
)

# =========================
# Tests
# =========================
add_custom_target(test
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build/test.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build
        DEPENDS GemVM
)

# =========================
# Install
# =========================
install(TARGETS GemVM DESTINATION /usr/local/bin)
