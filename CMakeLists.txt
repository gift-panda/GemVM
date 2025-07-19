cmake_minimum_required(VERSION 3.31)
project(GemVM)

set(CMAKE_CXX_STANDARD 23)
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

add_compile_options(-O3)

add_executable(GemVM main.c
        common.h
        chunk.h
        chunk.c
        memory.h
        memory.c
        debug.h
        debug.c
        value.h
        value.c
        vm.h
        vm.c
        compiler.h
        compiler.h
        compiler.c
        scanner.c
        scanner.h
        object.c
        object.h
        table.c
        table.h
        stringMethods.c
        listMethods.c
        windowMethods.c
        windowMethods.h
        Math.c
        Math.h
)

target_link_libraries(GemVM ${SDL2_LIBRARIES})
target_link_libraries(GemVM m)
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)

include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS})
target_link_libraries(GemVM SDL2::SDL2 SDL2_image::SDL2_image)

