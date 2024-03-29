# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)
project(SDL2_ttf)

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
        log
        log
)

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2_ttf/android/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/include
)

add_definitions(-DGL_GLEXT_PROTOTYPES)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../freetype/ ${CMAKE_CURRENT_SOURCE_DIR}/../freetype/)

add_library( # Sets the name of the library.
        SDL2_ttf

        # Sets the library as a shared library.
        SHARED

        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2_ttf/android/src/glfont.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2_ttf/android/src/SDL_ttf.c
        )


target_link_libraries(
        SDL2_ttf
        freetype
        SDL2
        GLESv1_CM
        GLESv2
        android
        log
)