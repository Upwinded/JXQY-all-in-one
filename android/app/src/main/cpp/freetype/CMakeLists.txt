# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)
project(freetype)

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

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/include)

add_definitions(-DFT_CONFIG_CONFIG_H=<ftconfig.h> -DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H=<ftmodule.h>)

add_library( # Sets the name of the library.
        freetype

        # Sets the library as a shared library.
        SHARED

        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/sfnt/sfobjs.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/sfnt/sfntpic.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/sfnt/sfdriver.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/sfnt/ttmtx.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/sfnt/ttload.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/sfnt/ttbdf.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/sfnt/ttpost.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/sfnt/ttcmap.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/sfnt/ttkern.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/sfnt/ttsbit.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/md5.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftglyph.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftotval.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftinit.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftbdf.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftgasp.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftsystem.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftstroke.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftapi.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftlcdfil.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftdebug.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftcid.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftfstype.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftxf86.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftbitmap.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftpfr.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftwinfnt.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftgxval.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftpatent.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftbbox.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftbase.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftsynth.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/fttype1.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/base/ftmm.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/smooth/ftsmooth.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/smooth/ftgrays.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/smooth/ftspic.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/raster/ftraster.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/raster/rastpic.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/raster/ftrend1.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/bzip2/ftbzip2.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/truetype/ttobjs.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/truetype/ttgload.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/truetype/ttpic.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/truetype/ttgxvar.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/truetype/ttsubpix.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/truetype/ttinterp.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/truetype/ttdriver.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/freetype/src/truetype/ttpload.c
        )


target_link_libraries(
        freetype
        GLESv1_CM
        GLESv2
        android
        z
        log
)