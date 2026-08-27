cmake_minimum_required(VERSION 3.15)

set(PROJECT_NAME "jxqy-all-in-one")
include(cmake/JxqyEngineVersion.cmake)

project(${PROJECT_NAME} VERSION ${JXQY_ENGINE_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(JXQY_FORCE_MOBILE_UI
    "Compile mobile touch-control branches in a desktop surrogate build"
    OFF)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_options($<$<CONFIG:Release>:/O2>)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wno-multichar)
    add_compile_options($<$<CONFIG:Release>:-O2>)
endif()

if(WIN32)
    add_compile_definitions(WIN32)
elseif(UNIX AND NOT APPLE)
    add_compile_definitions(__LINUX__)
    add_compile_definitions(LUA_USE_LINUX)
    add_compile_definitions(USE_FFMPEG4)
    set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)
elseif(APPLE)
    add_compile_definitions(__MACOS__)
endif()

if(JXQY_FORCE_MOBILE_UI)
    add_compile_definitions(__MOBILE__)
endif()

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty)
set(JXQY_MINIZ_SOURCES
    ${THIRD_PARTY_DIR}/miniz/miniz.c
    ${THIRD_PARTY_DIR}/miniz/miniz_zip.c
    ${THIRD_PARTY_DIR}/miniz/miniz_tinfl.c
    ${THIRD_PARTY_DIR}/miniz/miniz_tdef.c
)
set(JXQY_MOD_RELEASE_METADATA_SOURCES
    ${SRC_DIR}/Resource/ModReleaseMetadata.cpp
    ${SRC_DIR}/Resource/SemanticVersion.cpp
    ${SRC_DIR}/File/StrictRelativeResourcePath.cpp
)

include_directories(
    ${THIRD_PARTY_DIR}/miniz
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake
)

if(WIN32)
    set(3RD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rd)

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(LIB_ARCH_DIR x64)
        set(_JXQY_DEFAULT_VCPKG_TRIPLET x64-windows)
        set(_JXQY_WINDOWS_RUNTIME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin/win64)
    else()
        set(LIB_ARCH_DIR x86)
        set(_JXQY_DEFAULT_VCPKG_TRIPLET x86-windows)
        set(_JXQY_WINDOWS_RUNTIME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin/win32)
    endif()

    if(DEFINED VCPKG_TARGET_TRIPLET AND NOT VCPKG_TARGET_TRIPLET STREQUAL "")
        set(_JXQY_VCPKG_TRIPLET ${VCPKG_TARGET_TRIPLET})
    else()
        set(_JXQY_VCPKG_TRIPLET ${_JXQY_DEFAULT_VCPKG_TRIPLET})
    endif()

    # Prefer the bundled SDL3 runtime. Override by passing -DSDL3_ROOT=<path> at configure time.
    if(NOT DEFINED SDL3_ROOT OR SDL3_ROOT STREQUAL "")
        if(EXISTS "${THIRD_PARTY_DIR}/devel/win/SDL/include/SDL3/SDL.h")
            set(SDL3_ROOT "${THIRD_PARTY_DIR}/devel/win/SDL")
        elseif(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/installed/${_JXQY_VCPKG_TRIPLET}/include/SDL3/SDL.h")
            set(SDL3_ROOT "$ENV{VCPKG_ROOT}/installed/${_JXQY_VCPKG_TRIPLET}")
        elseif(EXISTS "C:/Tools/vcpkg/installed/${_JXQY_DEFAULT_VCPKG_TRIPLET}/include/SDL3/SDL.h")
            set(SDL3_ROOT "C:/Tools/vcpkg/installed/${_JXQY_DEFAULT_VCPKG_TRIPLET}")
        endif()
    endif()

    if(NOT DEFINED SDL3_ROOT OR NOT EXISTS "${SDL3_ROOT}/include/SDL3/SDL.h")
        message(FATAL_ERROR
            "SDL3 ${LIB_ARCH_DIR} development files were not found. "
            "Provide ThirdParty/devel/win/SDL or configure with -DSDL3_ROOT=<matching-architecture-root>.")
    endif()

    if(NOT DEFINED FFMPEG_ROOT OR FFMPEG_ROOT STREQUAL "")
        if(EXISTS "${THIRD_PARTY_DIR}/devel/win/ffmpeg/lib/${LIB_ARCH_DIR}/avcodec.lib")
            set(FFMPEG_ROOT "${THIRD_PARTY_DIR}/devel/win/ffmpeg")
        elseif(EXISTS "${3RD_DIR}/ffmpeg/win/lib/${LIB_ARCH_DIR}/avcodec.lib")
            set(FFMPEG_ROOT "${3RD_DIR}/ffmpeg/win")
        endif()
    endif()

    if(NOT DEFINED FFMPEG_ROOT OR NOT EXISTS "${FFMPEG_ROOT}/include/libavcodec/avcodec.h")
        message(FATAL_ERROR
            "FFmpeg ${LIB_ARCH_DIR} development files were not found. "
            "Provide ThirdParty/devel/win/ffmpeg or configure with -DFFMPEG_ROOT=<matching-architecture-root>.")
    endif()

    include_directories(
        ${SRC_DIR}
        ${THIRD_PARTY_DIR}/minilzo
        ${THIRD_PARTY_DIR}/lua
        ${FFMPEG_ROOT}/include
        ${SDL3_ROOT}/include
        # resource.h（Windows 资源头）位于 win/jxqy-all-in-one/ 下，
        # EngineBase.cpp 用 #include <resource.h> 引用；不复制文件，
        # 通过 include 目录解析，避免机器特定绝对路径。
        ${CMAKE_CURRENT_SOURCE_DIR}/win/jxqy-all-in-one
    )

    if(EXISTS "${SDL3_ROOT}/lib/${LIB_ARCH_DIR}/SDL3.lib")
        set(_SDL3_LIB_DIR ${SDL3_ROOT}/lib/${LIB_ARCH_DIR})
        set(_SDL3_BIN_DIR ${SDL3_ROOT}/lib/${LIB_ARCH_DIR})
    else()
        set(_SDL3_LIB_DIR ${SDL3_ROOT}/lib)
        set(_SDL3_BIN_DIR ${SDL3_ROOT}/bin)
    endif()

    set(SDL3_LIB ${_SDL3_LIB_DIR}/SDL3.lib)
    set(SDL3_IMAGE_LIB ${_SDL3_LIB_DIR}/SDL3_image.lib)
    set(SDL3_TTF_LIB ${_SDL3_LIB_DIR}/SDL3_ttf.lib)
    set(SDL3_MIXER_LIB ${_SDL3_LIB_DIR}/SDL3_mixer.lib)

    # 运行时 DLL：IMPORTED_LOCATION（SHARED IMPORTED 目标的运行时位置）应指向 .dll，
    # IMPORTED_IMPLIB 才指向 .lib 导入库。同时记录部署源 DLL 路径，供下方 file(COPY) 使用。
    set(SDL3_DLL ${_SDL3_BIN_DIR}/SDL3.dll)
    set(SDL3_IMAGE_DLL ${_SDL3_BIN_DIR}/SDL3_image.dll)
    set(SDL3_TTF_DLL ${_SDL3_BIN_DIR}/SDL3_ttf.dll)
    set(SDL3_MIXER_DLL ${_SDL3_BIN_DIR}/SDL3_mixer.dll)

    foreach(_SDL_REQUIRED_FILE
            "${SDL3_LIB}"
            "${SDL3_IMAGE_LIB}"
            "${SDL3_TTF_LIB}"
            "${SDL3_MIXER_LIB}"
            "${SDL3_DLL}"
            "${SDL3_IMAGE_DLL}"
            "${SDL3_TTF_DLL}"
            "${SDL3_MIXER_DLL}")
        if(NOT EXISTS "${_SDL_REQUIRED_FILE}")
            message(FATAL_ERROR "Required SDL3 ${LIB_ARCH_DIR} file is missing: ${_SDL_REQUIRED_FILE}")
        endif()
    endforeach()

    set(FFMPEG_LIBS
        ${FFMPEG_ROOT}/lib/${LIB_ARCH_DIR}/avcodec.lib
        ${FFMPEG_ROOT}/lib/${LIB_ARCH_DIR}/avformat.lib
        ${FFMPEG_ROOT}/lib/${LIB_ARCH_DIR}/avutil.lib
        ${FFMPEG_ROOT}/lib/${LIB_ARCH_DIR}/swresample.lib
        ${FFMPEG_ROOT}/lib/${LIB_ARCH_DIR}/swscale.lib
    )

    foreach(_FFMPEG_REQUIRED_FILE IN LISTS FFMPEG_LIBS)
        if(NOT EXISTS "${_FFMPEG_REQUIRED_FILE}")
            message(FATAL_ERROR "Required FFmpeg ${LIB_ARCH_DIR} import library is missing: ${_FFMPEG_REQUIRED_FILE}")
        endif()
    endforeach()

elseif(UNIX AND NOT APPLE)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _JXQY_LINUX_PROCESSOR)
    if(_JXQY_LINUX_PROCESSOR STREQUAL "amd64" OR
        _JXQY_LINUX_PROCESSOR STREQUAL "x64")
        set(_JXQY_LINUX_PROCESSOR "x86_64")
    endif()

    set(JXQY_LINUX_DEPENDENCY_ROOT
        "${THIRD_PARTY_DIR}/devel/linux/${_JXQY_LINUX_PROCESSOR}")
    set(JXQY_LINUX_INCLUDE_DIR "${JXQY_LINUX_DEPENDENCY_ROOT}/include")
    set(JXQY_LINUX_LIBRARY_DIR "${JXQY_LINUX_DEPENDENCY_ROOT}/lib")

    set(CURL_USE_STATIC_LIBS OFF)
    find_package(CURL 7.61 REQUIRED)

    set(_JXQY_LINUX_REQUIRED_FILES
        include/SDL3/SDL.h
        include/SDL3_image/SDL_image.h
        include/SDL3_ttf/SDL_ttf.h
        include/SDL3_mixer/SDL_mixer.h
        include/libavcodec/avcodec.h
        include/libavformat/avformat.h
        include/libavutil/avutil.h
        include/libswresample/swresample.h
        include/libswscale/swscale.h
        lib/libSDL3.so
        lib/libSDL3.so.0
        lib/libSDL3.so.0.4.10
        lib/libSDL3_image.so
        lib/libSDL3_image.so.0
        lib/libSDL3_image.so.0.2.4
        lib/libSDL3_ttf.so
        lib/libSDL3_ttf.so.0
        lib/libSDL3_ttf.so.0.2.2
        lib/libSDL3_mixer.so
        lib/libSDL3_mixer.so.0
        lib/libSDL3_mixer.so.0.2.4
        lib/libavcodec.so
        lib/libavcodec.so.59
        lib/libavcodec.so.59.37.100
        lib/libavformat.so
        lib/libavformat.so.59
        lib/libavformat.so.59.27.100
        lib/libavutil.so
        lib/libavutil.so.57
        lib/libavutil.so.57.28.100
        lib/libswresample.so
        lib/libswresample.so.4
        lib/libswresample.so.4.7.100
        lib/libswscale.so
        lib/libswscale.so.6
        lib/libswscale.so.6.7.100
    )
    foreach(_JXQY_LINUX_REQUIRED_FILE IN LISTS _JXQY_LINUX_REQUIRED_FILES)
        if(NOT EXISTS
            "${JXQY_LINUX_DEPENDENCY_ROOT}/${_JXQY_LINUX_REQUIRED_FILE}")
            message(FATAL_ERROR
                "Required private Linux dependency is missing: "
                "${JXQY_LINUX_DEPENDENCY_ROOT}/${_JXQY_LINUX_REQUIRED_FILE}\n"
                "Run linux/build-dependencies.sh before configuring.")
        endif()
    endforeach()

    include_directories(
        ${SRC_DIR}
        ${THIRD_PARTY_DIR}/minilzo
        ${THIRD_PARTY_DIR}/lua
        ${JXQY_LINUX_INCLUDE_DIR}
    )

    function(jxqy_add_linux_imported_library TARGET_NAME LIBRARY_NAME)
        add_library(${TARGET_NAME} SHARED IMPORTED)
        set_target_properties(${TARGET_NAME} PROPERTIES
            IMPORTED_LOCATION
                "${JXQY_LINUX_LIBRARY_DIR}/${LIBRARY_NAME}"
            INTERFACE_INCLUDE_DIRECTORIES
                "${JXQY_LINUX_INCLUDE_DIR}"
        )
    endfunction()

    jxqy_add_linux_imported_library(SDL3 libSDL3.so)
    jxqy_add_linux_imported_library(SDL3_image libSDL3_image.so)
    jxqy_add_linux_imported_library(SDL3_ttf libSDL3_ttf.so)
    jxqy_add_linux_imported_library(SDL3_mixer libSDL3_mixer.so)
    jxqy_add_linux_imported_library(FFmpeg_avcodec libavcodec.so)
    jxqy_add_linux_imported_library(FFmpeg_avformat libavformat.so)
    jxqy_add_linux_imported_library(FFmpeg_avutil libavutil.so)
    jxqy_add_linux_imported_library(FFmpeg_swresample libswresample.so)
    jxqy_add_linux_imported_library(FFmpeg_swscale libswscale.so)

    set(JXQY_LINUX_MEDIA_LIBRARIES
        SDL3
        SDL3_image
        SDL3_ttf
        SDL3_mixer
        FFmpeg_avcodec
        FFmpeg_avformat
        FFmpeg_avutil
        FFmpeg_swresample
        FFmpeg_swscale
    )
endif()

file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
    ${SRC_DIR}/*.cpp
    ${SRC_DIR}/*.c
    ${THIRD_PARTY_DIR}/minilzo/*.c
    ${THIRD_PARTY_DIR}/lua/*.c
)
list(APPEND SOURCE_FILES ${JXQY_MINIZ_SOURCES})

list(REMOVE_ITEM SOURCE_FILES
    ${THIRD_PARTY_DIR}/minilzo/testmini.c
    ${THIRD_PARTY_DIR}/lua/onelua.c
    ${THIRD_PARTY_DIR}/lua/lua.c
)

# Keep workspace-only sources outside the production executable even when they
# are present in a developer checkout.
file(GLOB_RECURSE SRC_TEST_FILES ${SRC_DIR}/tests/*)
list(REMOVE_ITEM SOURCE_FILES ${SRC_TEST_FILES})

file(GLOB_RECURSE SRC_AUTOMATION_FILES ${SRC_DIR}/automation/*)
list(REMOVE_ITEM SOURCE_FILES ${SRC_AUTOMATION_FILES})

file(GLOB_RECURSE EXCLUDE_TESTES_FILES "${THIRD_PARTY_DIR}/lua/testes/*")
list(REMOVE_ITEM SOURCE_FILES ${EXCLUDE_TESTES_FILES})

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(JXQY_WINDOWS_EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin/win64)
    else()
        set(JXQY_WINDOWS_EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin/win32)
    endif()

    # MSVC AddressSanitizer and ordinary Debug binaries are ABI-incompatible.
    # Keep them in separate runtime directories so switching build trees cannot
    # leave a stale ASan executable or runtime DLL in the normal Debug output.
    set(JXQY_ADDRESS_SANITIZER_ENABLED OFF)
    set(JXQY_CONFIGURED_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_DEBUG}")
    if(JXQY_FORCE_MOBILE_UI)
        # A desktop surrogate build exercises __MOBILE__ branches but is not a
        # distributable Windows game. Keep it out of the ordinary runtime tree
        # so it cannot overwrite the desktop executable used by smoke tests.
        set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/runtime)
    elseif(JXQY_CONFIGURED_CXX_FLAGS MATCHES "[/ -]fsanitize[=:]address")
        set(JXQY_ADDRESS_SANITIZER_ENABLED ON)
        set(EXECUTABLE_OUTPUT_PATH ${JXQY_WINDOWS_EXECUTABLE_OUTPUT_PATH}/asan)
    else()
        set(EXECUTABLE_OUTPUT_PATH ${JXQY_WINDOWS_EXECUTABLE_OUTPUT_PATH})
    endif()
elseif(UNIX AND NOT APPLE)
    set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin/linux)
elseif(APPLE)
    set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin/macos)
endif()

if(UNIX AND NOT APPLE)
    set(JXQY_LINUX_APPLICATION_ID "com.jxqy.jxqy-all-in-one")
    set(JXQY_LINUX_ICON_NAME "jxqy-all-in-one")
    set(JXQY_LINUX_ICON_SOURCE_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/macos_ios/jxqy/res")

    foreach(_JXQY_ICON_SIZE 16 32 64 128 256 512)
        set(_JXQY_ICON_SOURCE
            "${JXQY_LINUX_ICON_SOURCE_ROOT}/icon${_JXQY_ICON_SIZE}.png")
        if(NOT EXISTS "${_JXQY_ICON_SOURCE}")
            message(FATAL_ERROR
                "Required Linux application icon is missing: ${_JXQY_ICON_SOURCE}")
        endif()
        set(_JXQY_ICON_DESTINATION
            "${EXECUTABLE_OUTPUT_PATH}/share/icons/hicolor/${_JXQY_ICON_SIZE}x${_JXQY_ICON_SIZE}/apps")
        file(MAKE_DIRECTORY "${_JXQY_ICON_DESTINATION}")
        configure_file(
            "${_JXQY_ICON_SOURCE}"
            "${_JXQY_ICON_DESTINATION}/${JXQY_LINUX_ICON_NAME}.png"
            COPYONLY)
    endforeach()

    # SDL_SetWindowIcon loads this copy relative to the executable. Keep the
    # window icon at 128x128: a 256x256 ARGB _NET_WM_ICON property exceeds the
    # default maximum size of a single X11 request. The hicolor copies above
    # retain the larger sizes used by desktop shells and Wayland compositors.
    configure_file(
        "${JXQY_LINUX_ICON_SOURCE_ROOT}/icon128.png"
        "${EXECUTABLE_OUTPUT_PATH}/${JXQY_LINUX_ICON_NAME}.png"
        COPYONLY)
    set(JXQY_LINUX_DESKTOP_EXECUTABLE
        "${EXECUTABLE_OUTPUT_PATH}/${PROJECT_NAME}")
    set(JXQY_LINUX_DESKTOP_ICON
        "${EXECUTABLE_OUTPUT_PATH}/${JXQY_LINUX_ICON_NAME}.png")
    set(_JXQY_LINUX_DESKTOP_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/${JXQY_LINUX_APPLICATION_ID}.desktop")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/linux/jxqy-all-in-one.desktop.in"
        "${_JXQY_LINUX_DESKTOP_FILE}"
        @ONLY)
    file(COPY "${_JXQY_LINUX_DESKTOP_FILE}"
        DESTINATION "${EXECUTABLE_OUTPUT_PATH}"
        FILE_PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
endif()

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32
        ${SOURCE_FILES}
        ${CMAKE_CURRENT_SOURCE_DIR}/win/jxqy-all-in-one/jxqy-all-in-one.rc)

    # Windows 下 SDL3 为 SHARED IMPORTED 目标：IMPORTED_IMPLIB 指向导入库（.lib），
    # 链接器据此解析符号；IMPORTED_LOCATION 指向运行时 DLL，标识目标运行时位置，
    # 也被 VS_DEBUGGER_COMMAND 等机制用于定位可运行文件。两者必须分开赋值，
    # 否则只设置 IMPORTED_LOCATION 时 CMake 会把 .lib 当成 DLL 位置、实际导入库
    # 解析为空，生成 SDL3_lib-NOTFOUND 等链接项。
    add_library(SDL3_lib SHARED IMPORTED)
    set_target_properties(SDL3_lib PROPERTIES
        IMPORTED_IMPLIB ${SDL3_LIB}
        IMPORTED_LOCATION ${SDL3_DLL})

    add_library(SDL3_image_lib SHARED IMPORTED)
    set_target_properties(SDL3_image_lib PROPERTIES
        IMPORTED_IMPLIB ${SDL3_IMAGE_LIB}
        IMPORTED_LOCATION ${SDL3_IMAGE_DLL})

    add_library(SDL3_ttf_lib SHARED IMPORTED)
    set_target_properties(SDL3_ttf_lib PROPERTIES
        IMPORTED_IMPLIB ${SDL3_TTF_LIB}
        IMPORTED_LOCATION ${SDL3_TTF_DLL})

    add_library(SDL3_mixer_lib SHARED IMPORTED)
    set_target_properties(SDL3_mixer_lib PROPERTIES
        IMPORTED_IMPLIB ${SDL3_MIXER_LIB}
        IMPORTED_LOCATION ${SDL3_MIXER_DLL})

    target_link_libraries(${PROJECT_NAME}
        SDL3_lib
        SDL3_image_lib
        SDL3_ttf_lib
        SDL3_mixer_lib
        winhttp
        ${FFMPEG_LIBS}
    )

    # 把运行时依赖 DLL 在链接后复制到可执行文件输出目录（按配置子目录 Debug/Release），
    # 保证链接产物可直接运行。POST_BUILD 使用生成器表达式 $<TARGET_FILE_DIR:...>，
    # 在多配置生成器（VS）下会正确解析到对应配置的输出目录。
    # SDL3 间接依赖 vcpkg 提供的 freetype/zlib/brotli/libpng16 等 DLL，故在配置期用
    # file(GLOB) 收集 vcpkg bin 目录下全部 SDL3/字体/压缩相关 DLL，一并加入部署列表。
    file(GLOB _SDL_RUNTIME_DLLS
        "${_SDL3_BIN_DIR}/SDL3.dll"
        "${_SDL3_BIN_DIR}/SDL3_image.dll"
        "${_SDL3_BIN_DIR}/SDL3_ttf.dll"
        "${_SDL3_BIN_DIR}/SDL3_mixer.dll"
        "${_SDL3_BIN_DIR}/freetype.dll"
        "${_SDL3_BIN_DIR}/zlib1.dll"
        "${_SDL3_BIN_DIR}/bz2.dll"
        "${_SDL3_BIN_DIR}/brotlicommon.dll"
        "${_SDL3_BIN_DIR}/brotlidec.dll"
        "${_SDL3_BIN_DIR}/brotlienc.dll"
        "${_SDL3_BIN_DIR}/libpng16.dll"
        "${_SDL3_BIN_DIR}/optional/libavif-16.dll"
        "${_SDL3_BIN_DIR}/optional/libogg-0.dll"
        "${_SDL3_BIN_DIR}/optional/libopus-0.dll"
        "${_SDL3_BIN_DIR}/optional/libopusfile-0.dll"
        "${_SDL3_BIN_DIR}/optional/libtiff-6.dll"
        "${_SDL3_BIN_DIR}/optional/libwavpack-1.dll"
        "${_SDL3_BIN_DIR}/optional/libwebp-7.dll"
        "${_SDL3_BIN_DIR}/optional/libwebpdemux-2.dll"
        "${_SDL3_BIN_DIR}/optional/libxmp.dll")
    set(_FFMPEG_RUNTIME_SEARCH_DIRS
        "${FFMPEG_ROOT}/lib/${LIB_ARCH_DIR}/bin"
        "${3RD_DIR}/ffmpeg/win/lib/${LIB_ARCH_DIR}/bin"
        "${THIRD_PARTY_DIR}/devel/win/ffmpeg/lib/${LIB_ARCH_DIR}/bin"
        "${_JXQY_WINDOWS_RUNTIME_DIR}")
    set(_FFMPEG_RUNTIME_DLLS)
    foreach(_FFMPEG_RUNTIME_NAME
            avcodec-59.dll
            avformat-59.dll
            avutil-57.dll
            swresample-4.dll
            swscale-6.dll)
        set(_FFMPEG_RUNTIME_FOUND "")
        foreach(_FFMPEG_RUNTIME_DIR IN LISTS _FFMPEG_RUNTIME_SEARCH_DIRS)
            if(EXISTS "${_FFMPEG_RUNTIME_DIR}/${_FFMPEG_RUNTIME_NAME}")
                set(_FFMPEG_RUNTIME_FOUND "${_FFMPEG_RUNTIME_DIR}/${_FFMPEG_RUNTIME_NAME}")
                break()
            endif()
        endforeach()
        if(_FFMPEG_RUNTIME_FOUND STREQUAL "")
            message(FATAL_ERROR
                "Required FFmpeg ${LIB_ARCH_DIR} runtime ${_FFMPEG_RUNTIME_NAME} was not found in: "
                "${_FFMPEG_RUNTIME_SEARCH_DIRS}")
        endif()
        list(APPEND _FFMPEG_RUNTIME_DLLS "${_FFMPEG_RUNTIME_FOUND}")
    endforeach()
    set(_RUNTIME_DLL_DEPS ${_SDL_RUNTIME_DLLS} ${_FFMPEG_RUNTIME_DLLS})
    if(JXQY_ADDRESS_SANITIZER_ENABLED)
        get_filename_component(_MSVC_LINKER_DIR "${CMAKE_LINKER}" DIRECTORY)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_MSVC_ASAN_RUNTIME_NAME clang_rt.asan_dynamic-x86_64.dll)
        else()
            set(_MSVC_ASAN_RUNTIME_NAME clang_rt.asan_dynamic-i386.dll)
        endif()
        find_file(_MSVC_ASAN_RUNTIME_DLL
            NAMES ${_MSVC_ASAN_RUNTIME_NAME}
            PATHS "${_MSVC_LINKER_DIR}"
            NO_DEFAULT_PATH)
        if(NOT _MSVC_ASAN_RUNTIME_DLL)
            message(FATAL_ERROR "MSVC AddressSanitizer runtime DLL was not found beside ${CMAKE_LINKER}")
        endif()
    endif()
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_RUNTIME_DLL_DEPS}
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        VERBATIM)

elseif(UNIX AND NOT APPLE)
    add_executable(${PROJECT_NAME} ${SOURCE_FILES})

    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME}
        ${JXQY_LINUX_MEDIA_LIBRARIES}
        CURL::libcurl
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )

    file(GLOB JXQY_LINUX_RUNTIME_LIBRARIES
        "${JXQY_LINUX_LIBRARY_DIR}/*.so"
        "${JXQY_LINUX_LIBRARY_DIR}/*.so.*"
    )
    file(COPY ${JXQY_LINUX_RUNTIME_LIBRARIES}
        DESTINATION "${EXECUTABLE_OUTPUT_PATH}/lib"
        FOLLOW_SYMLINK_CHAIN
    )

    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "\$ORIGIN/lib"
        INSTALL_RPATH "\$ORIGIN/lib"
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH_USE_LINK_PATH FALSE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(${PROJECT_NAME} PRIVATE
            -static-libstdc++
            -static-libgcc)
    endif()
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME_DEBUG "${PROJECT_NAME}-debug")


if(WIN32 OR (UNIX AND NOT APPLE))
    add_executable(jxqy-program-updater
        ${CMAKE_CURRENT_SOURCE_DIR}/updater/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/updater/DesktopProgramUpdater.cpp
    )
    target_include_directories(jxqy-program-updater PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/updater)
    if(WIN32)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(JXQY_PROGRAM_UPDATER_OUTPUT
                "${CMAKE_CURRENT_SOURCE_DIR}/bin/updater/win64")
        else()
            set(JXQY_PROGRAM_UPDATER_OUTPUT
                "${CMAKE_CURRENT_SOURCE_DIR}/bin/updater/win32")
        endif()
    else()
        set(JXQY_PROGRAM_UPDATER_OUTPUT
            "${CMAKE_CURRENT_SOURCE_DIR}/bin/updater/linux")
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_link_options(jxqy-program-updater PRIVATE
                -static-libstdc++
                -static-libgcc)
        endif()
    endif()
    set_target_properties(jxqy-program-updater PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${JXQY_PROGRAM_UPDATER_OUTPUT}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${JXQY_PROGRAM_UPDATER_OUTPUT}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${JXQY_PROGRAM_UPDATER_OUTPUT}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${JXQY_PROGRAM_UPDATER_OUTPUT}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${JXQY_PROGRAM_UPDATER_OUTPUT}")
endif()

get_property(JXQY_DIRECTORY_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(JXQY_TARGET IN LISTS JXQY_DIRECTORY_TARGETS)
    # Every executable compiled with MSVC AddressSanitizer loads the dynamic
    # runtime before entering main(), so deploy it beside each executable.
    if(WIN32 AND JXQY_ADDRESS_SANITIZER_ENABLED)
        get_target_property(JXQY_TARGET_TYPE ${JXQY_TARGET} TYPE)
        if(JXQY_TARGET_TYPE STREQUAL "EXECUTABLE")
            add_custom_command(TARGET ${JXQY_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_MSVC_ASAN_RUNTIME_DLL}"
                    "$<TARGET_FILE_DIR:${JXQY_TARGET}>"
                VERBATIM)
        endif()
    endif()
endforeach()
