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
    chunk.c memory.c debug.c value.c vm.c compiler.c scanner.c object.c table.c
    stringMethods.c listMethods.c windowMethods.c Math.c linenoise.c
    serialize.c deserialize.c fileMethods.c
    deserializeBytecode.c deserializeMemory.c
)

# =========================
# Dependencies
# =========================
find_package(PkgConfig REQUIRED)

# SDL2 (real CMake target)
find_package(SDL2 REQUIRED)

# SDL2_image + SDL2_ttf via pkg-config (Arch way)
pkg_check_modules(SDL2IMAGE REQUIRED SDL2_image)
pkg_check_modules(SDL2TTF REQUIRED SDL2_ttf)

# Boehm GC
pkg_check_modules(GC REQUIRED bdw-gc)

# SDL2_gfx (no pkg-config on Arch)
find_library(SDL2_GFX_LIBRARY
    NAMES SDL2_gfx
    REQUIRED
)

# termbox2 (local)
find_library(TERMBOX2_LIBRARY
    NAMES termbox2
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/termbox2"
    REQUIRED
)

# =========================
# Executable
# =========================
add_executable(GemVM ${GEMVM_SOURCES})

target_compile_definitions(GemVM PRIVATE GC_THREADS)

# =========================
# Includes
# =========================
target_include_directories(GemVM PRIVATE
    ${SDL2IMAGE_INCLUDE_DIRS}
    ${SDL2TTF_INCLUDE_DIRS}
    ${GC_INCLUDE_DIRS}
    ../termbox2
)

# =========================
# Linking
# =========================
target_link_libraries(GemVM
    SDL2::SDL2
    ${SDL2IMAGE_LIBRARIES}
    ${SDL2TTF_LIBRARIES}
    ${SDL2_GFX_LIBRARY}
    ${TERMBOX2_LIBRARY}
    ${GC_LIBRARIES}
    m pthread
)

# =========================
# RPATH
# =========================
set_target_properties(GemVM PROPERTIES
    BUILD_RPATH "/usr/local/lib"
    INSTALL_RPATH "/usr/local/lib"
)

# =========================
# Install
# =========================
install(TARGETS GemVM DESTINATION /usr/local/bin)

