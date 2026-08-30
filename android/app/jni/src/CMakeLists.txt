cmake_minimum_required(VERSION 3.11)

project(jxqy_all_in_one)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ANDROID_STL "c++_shared")


include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/include

        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/minilzo
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/lua
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/miniz
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake
)

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
        libavcodec PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/lib/libavcodec.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/include
)
set_target_properties(
        libavformat PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/lib/libavformat.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/include
)
set_target_properties(
        libavutil PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/lib/libavutil.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/include
)
set_target_properties(
        libswresample PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/lib/libswresample.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/include
)
set_target_properties(
        libswscale PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/lib/libswscale.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/include
)
set_target_properties(
        libavfilter PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/lib/libavfilter.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/include
)
#set_target_properties(
#        libfdk-aac PROPERTIES
#        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/lib/libfdk-aac.so
#        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/include
#)
set_target_properties(
        libavdevice PROPERTIES
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/lib/libavdevice.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/include
)
#set_target_properties(
#        libpostproc PROPERTIES
#        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/lib/libpostproc.so
#        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/include
#)

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)
find_package(SDL3_mixer REQUIRED CONFIG)

file(GLOB_RECURSE source_files CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/minilzo/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/lua/*.c
        )

list(APPEND source_files
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/miniz/miniz.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/miniz/miniz_zip.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/miniz/miniz_tinfl.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/miniz/miniz_tdef.c
)

list( REMOVE_ITEM source_files
		${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/minilzo/testmini.c
		${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/lua/onelua.c
		${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/lua/lua.c
		${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/File/PakFile.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/Game/Data/NPC_old.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/Game/Data/Player_old.cpp
)

# Tests under src/tests belong to dedicated test targets and must never be
# compiled into the Android game library.
file(GLOB_RECURSE src_test_files
		"${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/tests/*"
)
list(REMOVE_ITEM source_files ${src_test_files})

# Windows-only automation launchers are separate helper targets in the root
# build and must not be compiled into the Android shared library.
file(GLOB_RECURSE src_automation_files
		"${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/automation/*"
)
list(REMOVE_ITEM source_files ${src_automation_files})

file(GLOB_RECURSE EXCLUDE_TESTES_FILES
		"${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/lua/testes/*"
)

list(REMOVE_ITEM source_files ${EXCLUDE_TESTES_FILES})

add_library( # Sets the name of the library.
        main

        # Sets the library as a shared library.
        SHARED

        # Provides a relative path to your source file(s).
        ${source_files}

)

#https://developer.android.com/guide/practices/page-sizes#update-packaging
target_link_options(main PRIVATE "-Wl,-z,max-page-size=16384")
target_link_options(main PRIVATE "-Wl,-z,common-page-size=16384")

target_link_libraries( # Specifies the target library.
        main
	PRIVATE
        SDL3::SDL3
	SDL3_image::SDL3_image
	SDL3_ttf::SDL3_ttf
	SDL3_mixer::SDL3_mixer
        libavcodec
        libavformat
        libavutil
        libswresample
        libswscale
        libavfilter
#       libfdk-aac
        libavdevice
#        libpostproc
        )
