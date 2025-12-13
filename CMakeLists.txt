cmake_minimum_required(VERSION 3.31)
project(GemVM)

set(CMAKE_CXX_STANDARD 23)
add_compile_definitions(TB_IMPL_V1)
set(CMAKE_BUILD_TYPE Debug)

# Add source files
set(GEMVM_SOURCES
        main.c
        common.h
        chunk.h chunk.c
        memory.h memory.c
        debug.h debug.c
        value.h value.c
        vm.h vm.c
        compiler.h compiler.c
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
        /home/meow/boot/compiler.gemc
)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C flags (debug): ${CMAKE_C_FLAGS_DEBUG}")
message(STATUS "CXX flags (debug): ${CMAKE_CXX_FLAGS_DEBUG}")

# Include directories
include_directories(
        /usr/local/include/SDL2
        /usr/local/include/SDL2_image
        /usr/local/include/SDL2_gfx
        /home/meow/CLionProjects/GemVM/termbox2
        /usr/local/include/cjson 
)

# Find libraries
find_library(SDL2_TTF_LIBRARY
        NAMES SDL2_ttf SDL_ttf
        PATHS /usr/lib /usr/local/lib /usr/lib64
)

# Define static libraries
set(SDL2_STATIC_LIB        "/usr/local/lib/libSDL2.a")
set(SDL2_IMAGE_STATIC_LIB  "/usr/local/lib/libSDL2_image.a")
set(SDL2_GFX_STATIC_LIB    "/usr/local/lib/libSDL2_gfx.a")   # ← Add this
set(TERMBOX_LIB            "/home/meow/CLionProjects/GemVM/termbox2/libtermbox2.a")

# Find cJSON library
find_library(CJSON_LIBRARY
        NAMES cjson
        PATHS /usr/lib /usr/local/lib /usr/lib64
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GC REQUIRED bdw-gc)

include_directories(${GC_INCLUDE_DIRS})
link_directories(${GC_LIBRARY_DIRS})

add_executable(GemVM ${GEMVM_SOURCES})

target_compile_definitions(GemVM PRIVATE GC_THREADS)

add_custom_target(test
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/build/test.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build
    DEPENDS GemVM
    COMMENT "Running GemVM tests..."
)

# Link libraries
target_link_libraries(GemVM
        ${SDL2_TTF_LIBRARY}
        ${SDL2_STATIC_LIB}
        ${SDL2_IMAGE_STATIC_LIB}
        ${SDL2_GFX_STATIC_LIB}
        ${TERMBOX_LIB}
        ${GC_LIBRARIES}
        m
        png
        jpeg
        z
        pthread
        freetype
)

