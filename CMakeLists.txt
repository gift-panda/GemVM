cmake_minimum_required(VERSION 3.31)
project(GemVM)

set(CMAKE_CXX_STANDARD 23)
add_compile_definitions(TB_IMPL_V1)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")# -g -fsanitize=address -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")# -g -fsanitize=address -fno-omit-frame-pointer")

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
)

set(STATIC_LINKAGE TRUE)

if(STATIC_LINKAGE)
        include_directories(
                /usr/local/include/SDL2
                /usr/local/include/SDL2_image
                /usr/local/include/SDL2_gfx     # ← Add this
                /home/meow/CLionProjects/GemVM/termbox2
        )

        set(SDL2_STATIC_LIB         "/usr/local/lib/libSDL2.a")
        set(SDL2_IMAGE_STATIC_LIB  "/usr/local/lib/libSDL2_image.a")
        set(SDL2_GFX_STATIC_LIB    "/usr/local/lib/libSDL2_gfx.a")   # ← Add this
        set(TERMBOX_LIB            "/home/meow/CLionProjects/GemVM/termbox2/libtermbox2.a")

        add_executable(GemVM ${GEMVM_SOURCES})

        target_link_libraries(GemVM
                ${SDL2_STATIC_LIB}
                ${SDL2_IMAGE_STATIC_LIB}
                ${SDL2_GFX_STATIC_LIB}    # ← Add this
                ${TERMBOX_LIB}
                m
                png
                jpeg
                z
                pthread
        )

else()
        find_package(SDL2 REQUIRED)
        find_package(SDL2_image REQUIRED)
        # SDL2_gfx has no official FindSDL2_gfx.cmake, so we do it manually:
        find_library(SDL2_GFX_LIB SDL2_gfx PATHS /usr/local/lib)        # ← Add this
        include_directories(/usr/local/include/SDL2_gfx)                # ← Add this

        add_subdirectory(termbox2)
        include_directories(
                ${SDL2_INCLUDE_DIRS}
                ${SDL2_IMAGE_INCLUDE_DIRS}
                termbox2
        )

        add_executable(GemVM ${GEMVM_SOURCES})

        target_link_libraries(GemVM
                SDL2::SDL2
                SDL2_image::SDL2_image
                ${SDL2_GFX_LIB}        # ← Add this
                termbox2
                m
        )
endif()
