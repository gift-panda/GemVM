cmake_minimum_required(VERSION 3.31)
project(GemVM)

set(STATIC_LINKAGE TRUE)
add_compile_definitions(TB_IMPL_V1)


if(NOT STATIC_LINKAGE)
        set(CMAKE_CXX_STANDARD 23)
        find_package(SDL2 REQUIRED)
        include_directories(${SDL2_INCLUDE_DIRS})

        add_subdirectory(termbox2)
        include_directories(termbox2)

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
                linenoise.c
                linenoise.h
        )

        target_link_libraries(GemVM ${SDL2_LIBRARIES})
        target_link_libraries(GemVM m)
        target_link_libraries(GemVM termbox2)

        find_package(SDL2 REQUIRED)
        find_package(SDL2_image REQUIRED)

        include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS})
        target_link_libraries(GemVM SDL2::SDL2 SDL2_image::SDL2_image)

endif()

if(STATIC_LINKAGE)
        cmake_minimum_required(VERSION 3.31)
        project(GemVM)

        set(CMAKE_CXX_STANDARD 23)

        # Prefer static libs for SDL2 and SDL2_image
        set(SDL2_STATIC ON)
        set(SDL2_IMAGE_STATIC ON)


        include_directories(
        /usr/local/include/SDL2
        /usr/local/include/SDL2_image
        /home/meow/CLionProjects/GemVM/termbox2
        )

        add_compile_options(-O3)

        add_executable(GemVM
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
        )

        # Example paths, change these to where your static libs actually are
        set(SDL2_STATIC_LIB "/usr/local/lib/libSDL2.a")
        set(SDL2_IMAGE_STATIC_LIB "/usr/local/lib/libSDL2_image.a")
        set(TERMBOX_LIB "/home/meow/CLionProjects/GemVM/termbox2/libtermbox2.a")


        target_link_libraries(GemVM
                ${SDL2_STATIC_LIB}
                ${SDL2_IMAGE_STATIC_LIB}
                ${TERMBOX_LIB}
                m
                png
                jpeg
                z
                pthread
        )

endif()
