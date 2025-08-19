cmake_minimum_required(VERSION 3.6)

project(jxqy_all_in_one)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ANDROID_STL "c++_shared")


include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/fmod/inc

        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/ffmpeg/${ANDROID_ABI}/include

        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/minilzo
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/lua
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
        IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/fmod/lib/${ANDROID_ABI}/libfmod.so
        INTERFACE_INCLUDE_DIRECTORIES  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/devel/android/fmod/inc
)

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

file(GLOB_RECURSE source_files
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/*
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/minilzo/*
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/lua/*
        )

list( REMOVE_ITEM source_files
		${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/minilzo/testmini.c
		${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/lua/onelua.c
		${CMAKE_CURRENT_SOURCE_DIR}/../../../../ThirdParty/lua/lua.c
)

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
        )