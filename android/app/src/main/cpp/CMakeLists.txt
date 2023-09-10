# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.10)

# Declares and names the project.

project("main")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ANDROID_STL "c++_shared")

set(LIB_FMOD ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/fmod/android/lib/${ANDROID_ABI}/libfmod.so)
set(LIB_LIBAVCODEC ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libavcodec.so)
set(LIB_LIBAVFORMAT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libavformat.so)
set(LIB_LIBAVUTIL ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libavutil.so)
set(LIB_LIBSWRESAMPLE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libswresample.so)
set(LIB_LIBSWSCALE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libswscale.so)
set(LIB_LIBAVFILTER ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libavfilter.so)
#set(LIB_LIBFDK_AAC ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libfdk-aac.so)
set(LIB_LIBAVDEVICE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libavdevice.so)
#set(LIB_LIBPOSTPROC ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libpostproc.so)


include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/fmod/android/inc

        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/${ANDROID_ABI}/include

        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/SDL2/android/include

        # SDL_internal.h is located in src required by SDL_android_main.c bridge
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/SDL2/android/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/SDL2/android/src/core/android

        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/SDL2_image/android/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/SDL2_ttf/android/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/minilzo
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/lua
)

add_library( fmod SHARED IMPORTED )
add_library( libavcodec SHARED IMPORTED )
add_library( libavformat SHARED IMPORTED )
add_library( libavutil SHARED IMPORTED )
add_library( libswresample SHARED IMPORTED )
add_library( libswscale SHARED IMPORTED )
add_library( libavfilter SHARED IMPORTED )
#add_library( libfdk-aac SHARED IMPORTED )
add_library( libavdevice SHARED IMPORTED )
#add_library( libpostproc SHARED IMPORTED )


set_target_properties(
        fmod PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/fmod/android/lib/${ANDROID_ABI}/libfmod.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/fmod/android/inc
)

set_target_properties(
        libavcodec PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libavcodec.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/include
)
set_target_properties(
        libavformat PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libavformat.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/include
)
set_target_properties(
        libavutil PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libavutil.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/include
)
set_target_properties(
        libswresample PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libswresample.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/include
)
set_target_properties(
        libswscale PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libswscale.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/include
)
set_target_properties(
        libavfilter PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libavfilter.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/include
)
#set_target_properties(
#        libfdk-aac PROPERTIES
#        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libfdk-aac.so
#        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/include
#)
set_target_properties(
        libavdevice PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libavdevice.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/include
)
#set_target_properties(
#        libpostproc PROPERTIES
#        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/lib/libpostproc.so
#        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/ffmpeg/android/${ANDROID_ABI}/include
#)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/SDL2 ${CMAKE_CURRENT_SOURCE_DIR}/SDL2)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/SDL2_image ${CMAKE_CURRENT_SOURCE_DIR}/SDL2_image)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/SDL2_ttf ${CMAKE_CURRENT_SOURCE_DIR}/SDL2_ttf)
#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/SDL2/ ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/SDL2)
#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/SDL2_image/ ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/SDL2_image)
#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/SDL2_ttf/ ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/SDL2_ttf)

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

file(GLOB_RECURSE source_files
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src/*
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/minilzo/*
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../3rd/lua/*
        )

add_library( # Sets the name of the library.
        main

        # Sets the library as a shared library.
        SHARED

        # Provides a relative path to your source file(s).
        ${source_files}

)

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.

find_library( # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
        main
        SDL2
        SDL2_image
        SDL2_ttf
        libavcodec
        libavformat
        libavutil
        libswresample
        libswscale
        libavfilter
#       libfdk-aac
        libavdevice
#        libpostproc
        fmod

        ${log-lib} )