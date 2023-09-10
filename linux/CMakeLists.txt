cmake_minimum_required(VERSION 3.10)

project("jxqy")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)

set(CMAKE_VERBOSE_MAKEFILE on)
set(CMAKE_CXX_FLAGS "-O2 -std=c++17 -pthread -DNDEBUG -D__LINUX__ -DUSE_FFMPEG4 -DLUA_USE_LINUX")

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/fmod/linux/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/minilzo
        ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/lua
        /usr/include/SDL2
        /usr/include
        /usr/include/x86_64-linux-gnu/SDL2
        /usr/include/x86_64-linux-gnu/
)

add_library( fmod SHARED IMPORTED )

set_target_properties(
        fmod PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/fmod/linux/lib/x86_64/libfmod.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/fmod/linux/inc
)


file( GLOB_RECURSE source_files
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/*
        ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/minilzo/*
        ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/lua/*
)

list( REMOVE_ITEM source_files ${CMAKE_CURRENT_SOURCE_DIR}/../3rd/minilzo/testmini.c )

SET(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../bin/linux)
add_executable( jxqy ${source_files} )

set_target_properties(jxqy PROPERTIES LINK_FLAGS "-Wl,-Bstatic -lstdc++ -lsupc++ -Wl,-Bdynamic -Wl,-rpath,.")

target_link_libraries(
        jxqy
        SDL2
        #SDL2main
        SDL2_image
        SDL2_ttf
        avcodec
        avformat
        avutil
        swresample
        swscale
        avfilter
        avdevice
        fmod
)
