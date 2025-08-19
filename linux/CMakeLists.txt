cmake_minimum_required(VERSION 3.10)

set(PROJECT_NAME "JXQY2")
set(PROJECT_VERSION 1.4.0)

project(${PROJECT_NAME} VERSION ${PROJECT_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)

set(CMAKE_VERBOSE_MAKEFILE on)
set(CMAKE_CXX_FLAGS "-O2 -pthread -DNDEBUG -D__LINUX__ -DUSE_FFMPEG4 -DLUA_USE_LINUX")

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/devel/linux/SDL/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/devel/linux/fmod/inc
        ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/minilzo
        ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/lua
        /usr/include
        /usr/include/x86_64-linux-gnu/
)

add_library( fmod SHARED IMPORTED )

set_target_properties(
        fmod PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/devel/linux/fmod/lib/x86_64/libfmod.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/devel/linux/fmod/inc
)

add_library( SDL3 SHARED IMPORTED )
set_target_properties(
        SDL3 PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/devel/linux/SDL/lib/libSDL3.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/devel/linux/SDL/include
)

add_library( SDL3_image SHARED IMPORTED )
set_target_properties(
        SDL3_image PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/devel/linux/SDL/lib/libSDL3_image.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/devel/linux/SDL/include
)

add_library( SDL3_ttf SHARED IMPORTED )
set_target_properties(
        SDL3_ttf PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/devel/linux/SDL/lib/libSDL3_ttf.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/devel/linux/SDL/include
)

file( GLOB_RECURSE source_files
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/*
        ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/minilzo/*
        ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/lua/*
)

list( REMOVE_ITEM source_files 
	${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/minilzo/testmini.c 
	${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/lua/onelua.c
	${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/lua/lua.c
)

file(GLOB_RECURSE EXCLUDE_TESTES_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/lua/testes/*"
)

# 从主文件列表中移除 testes 目录的文件
list(REMOVE_ITEM source_files ${EXCLUDE_TESTES_FILES})

SET(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../bin/linux)
add_executable( ${PROJECT_NAME} ${source_files} )

set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-Wl,-Bstatic -lstdc++ -lsupc++ -Wl,-Bdynamic -Wl,-rpath,.")

target_link_libraries(
        ${PROJECT_NAME}
        SDL3
        SDL3_image
        SDL3_ttf
        avcodec
        avformat
        avutil
        swresample
        swscale
        avfilter
        avdevice
        fmod
)
