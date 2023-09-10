# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)
project(libpng)

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.

find_library( # Sets the name of the path variable.
        GLESv1_CM

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        GLESv1_CM )

find_library(
        GLESv2
        GLESv2
)

find_library(
        android
        android
)

find_library(
        z
        z
)

find_library(
        log
        log
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/libpng/include)

add_definitions(-DGL_GLEXT_PROTOTYPES -DANDROID)

file(GLOB
        source_files
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/libpng/src/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/libpng/src/arm/*
        )

add_library( # Sets the name of the library.
        libpng

        # Sets the library as a shared library.
        SHARED

        ${source_files}
        )

target_link_libraries(
        libpng
        SDL2
        GLESv1_CM
        GLESv2
        android
        z
        log
)