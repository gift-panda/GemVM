cmake_minimum_required(VERSION 3.31)
project(GemVM)

set(CMAKE_CXX_STANDARD 23)

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
)
