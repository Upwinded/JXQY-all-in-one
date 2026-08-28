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
option(JXQY_BUILD_AUTOMATION_TOOLS
    "Build desktop automation helper executables"
    OFF)
option(JXQY_ENABLE_AUTOMATION_HOOKS
    "Compile test-only game automation interfaces"
    OFF)
option(JXQY_BUILD_BENCHMARKS
    "Build benchmark targets"
    OFF)
option(BUILD_TESTING
    "Build the test targets"
    OFF)

if(BUILD_TESTING OR JXQY_ENABLE_AUTOMATION_HOOKS)
    add_compile_definitions(JXQY_ENABLE_AUTOMATION_HOOKS)
endif()
if(BUILD_TESTING)
    add_compile_definitions(JXQY_ENABLE_TEST_HOOKS)
endif()

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_options($<$<CONFIG:Release>:/O2>)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

    function(jxqy_use_static_msvc_runtime TARGET_NAME)
        set_property(TARGET ${TARGET_NAME} PROPERTY
            MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endfunction()
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

# Tests under src/tests belong to dedicated test targets and must never be
# compiled into the game executable.
file(GLOB_RECURSE SRC_TEST_FILES ${SRC_DIR}/tests/*)
list(REMOVE_ITEM SOURCE_FILES ${SRC_TEST_FILES})

# Automation entry points are built as separate helper executables.
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
    if(MSVC AND NOT JXQY_ADDRESS_SANITIZER_ENABLED)
        jxqy_use_static_msvc_runtime(${PROJECT_NAME})
    endif()

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
    set(_MSVC_REQUIRED_RUNTIME_DLLS)
    if(MSVC)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
        include(InstallRequiredSystemLibraries)
        foreach(_MSVC_RUNTIME_DLL IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
            get_filename_component(
                _MSVC_RUNTIME_NAME "${_MSVC_RUNTIME_DLL}" NAME)
            string(TOLOWER
                "${_MSVC_RUNTIME_NAME}" _MSVC_RUNTIME_NAME_LOWER)
            if(_MSVC_RUNTIME_NAME_LOWER STREQUAL "vcruntime140.dll")
                list(APPEND _MSVC_REQUIRED_RUNTIME_DLLS
                    "${_MSVC_RUNTIME_DLL}")
            endif()
        endforeach()
        if(NOT _MSVC_REQUIRED_RUNTIME_DLLS)
            message(FATAL_ERROR
                "Required MSVC redistributable vcruntime140.dll was not found")
        endif()
    endif()
    set(_RUNTIME_DLL_DEPS
        ${_SDL_RUNTIME_DLLS}
        ${_FFMPEG_RUNTIME_DLLS}
        ${_MSVC_REQUIRED_RUNTIME_DLLS})
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

if(WIN32)
    add_executable(jxqy-starter WIN32
        ${CMAKE_CURRENT_SOURCE_DIR}/starter/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/win/jxqy-starter/jxqy-starter.rc)
    if(MSVC)
        jxqy_use_static_msvc_runtime(jxqy-starter)
    endif()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(JXQY_STARTER_OUTPUT
            "${CMAKE_CURRENT_SOURCE_DIR}/bin/starter/win64")
    else()
        set(JXQY_STARTER_OUTPUT
            "${CMAKE_CURRENT_SOURCE_DIR}/bin/starter/win32")
    endif()
    set_target_properties(jxqy-starter PROPERTIES
        OUTPUT_NAME "jxqy-all-in-one"
        RUNTIME_OUTPUT_DIRECTORY "${JXQY_STARTER_OUTPUT}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${JXQY_STARTER_OUTPUT}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${JXQY_STARTER_OUTPUT}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${JXQY_STARTER_OUTPUT}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${JXQY_STARTER_OUTPUT}")
endif()

if(WIN32 AND JXQY_BUILD_AUTOMATION_TOOLS)
    add_executable(jxqy-automation-launcher WIN32
        ${SRC_DIR}/automation/jxqy_automation_launcher.cpp
    )
    target_compile_definitions(jxqy-automation-launcher PRIVATE
        JXQY_GAME_EXECUTABLE_NAME="$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    )
    add_dependencies(jxqy-automation-launcher ${PROJECT_NAME})
endif()

if(WIN32 OR (UNIX AND NOT APPLE))
    add_executable(jxqy-program-updater
        ${CMAKE_CURRENT_SOURCE_DIR}/updater/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/updater/DesktopProgramUpdater.cpp
    )
    if(MSVC)
        jxqy_use_static_msvc_runtime(jxqy-program-updater)
    endif()
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

include(CTest)
if(BUILD_TESTING)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(EXISTS
        "${CMAKE_CURRENT_SOURCE_DIR}/private-build/scripts/test_package_desktop_release.py")
    add_test(NAME jxqy-desktop-release-package-tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/private-build/scripts/test_package_desktop_release.py)
endif()

if(EXISTS
        "${CMAKE_CURRENT_SOURCE_DIR}/private-build/scripts/test_package_android_release.py")
    add_test(NAME jxqy-android-release-package-tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/private-build/scripts/test_package_android_release.py)
endif()

if(EXISTS
        "${CMAKE_CURRENT_SOURCE_DIR}/private-build/scripts/test_package_apple_release.py")
    add_test(NAME jxqy-apple-release-package-tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/private-build/scripts/test_package_apple_release.py)
endif()

if(EXISTS
        "${CMAKE_CURRENT_SOURCE_DIR}/private-build/scripts/test_package_third_party_dependencies.py")
    add_test(NAME jxqy-third-party-release-package-tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/private-build/scripts/test_package_third_party_dependencies.py)
endif()

# Runtime trap-index contract test. Keep this target intentionally small so it
# validates Traps and INI persistence without linking the full game executable.
add_executable(jxqy-trap-tests
    ${SRC_DIR}/tests/test_traps.cpp
    ${SRC_DIR}/Game/Data/Traps.cpp
    ${SRC_DIR}/File/INIReader.cpp
    ${SRC_DIR}/File/ini.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)

if(WIN32)
    target_link_libraries(jxqy-trap-tests SDL3_lib)
    add_custom_command(TARGET jxqy-trap-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:jxqy-trap-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-trap-tests SDL3)
endif()

add_executable(jxqy-mod-release-metadata-tests
    ${SRC_DIR}/tests/test_mod_release_metadata.cpp
    ${JXQY_MOD_RELEASE_METADATA_SOURCES}
    ${SRC_DIR}/File/RootedResourceReader.cpp
    ${SRC_DIR}/Resource/ModReleaseAssets.cpp
)
add_test(NAME jxqy-mod-release-metadata-tests
    COMMAND jxqy-mod-release-metadata-tests)

add_executable(jxqy-online-update-catalog-tests
    ${SRC_DIR}/tests/test_online_update_catalog.cpp
    ${SRC_DIR}/Update/OnlineUpdateCatalog.cpp
    ${SRC_DIR}/Update/ResourceDownloadPlanner.cpp
    ${SRC_DIR}/Update/ArtifactChecksum.cpp
    ${SRC_DIR}/Resource/SemanticVersion.cpp
    ${SRC_DIR}/Resource/ModReleaseMetadata.cpp
    ${SRC_DIR}/File/StrictRelativeResourcePath.cpp
    ${SRC_DIR}/File/ini.cpp
    ${JXQY_MINIZ_SOURCES}
)
add_test(NAME jxqy-online-update-catalog-tests
    COMMAND jxqy-online-update-catalog-tests)

add_executable(jxqy-desktop-program-updater-tests
    ${SRC_DIR}/tests/test_desktop_program_updater.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/updater/DesktopProgramUpdater.cpp
)
target_include_directories(jxqy-desktop-program-updater-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/updater)
add_test(NAME jxqy-desktop-program-updater-tests
    COMMAND jxqy-desktop-program-updater-tests)

add_executable(jxqy-resource-package-archive-tests
    ${SRC_DIR}/tests/test_resource_package_archive.cpp
    ${SRC_DIR}/Update/ResourceDownloadPreparation.cpp
    ${SRC_DIR}/Update/ResourceDownloadPlanner.cpp
    ${SRC_DIR}/Update/ResourceInstallTransaction.cpp
    ${SRC_DIR}/Update/ResourcePackageArchive.cpp
    ${SRC_DIR}/Update/HttpsDownload.cpp
    ${SRC_DIR}/Update/OnlineUpdateCatalog.cpp
    ${SRC_DIR}/Update/ArtifactChecksum.cpp
    ${SRC_DIR}/Resource/ResourceManifest.cpp
    ${SRC_DIR}/Resource/ModReleaseMetadata.cpp
    ${SRC_DIR}/Resource/SemanticVersion.cpp
    ${SRC_DIR}/File/StrictRelativeResourcePath.cpp
    ${SRC_DIR}/File/ini.cpp
    ${JXQY_MINIZ_SOURCES}
)
target_compile_definitions(jxqy-resource-package-archive-tests PRIVATE
    JXQY_RESOURCE_MANIFEST_BUFFER_ONLY)
if(WIN32)
    target_link_libraries(jxqy-resource-package-archive-tests PRIVATE winhttp)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-resource-package-archive-tests PRIVATE CURL::libcurl)
endif()
add_test(NAME jxqy-resource-package-archive-tests
    COMMAND jxqy-resource-package-archive-tests)

add_executable(jxqy-resource-install-transaction-tests
    ${SRC_DIR}/tests/test_resource_install_transaction.cpp
    ${SRC_DIR}/Update/ResourceInstallTransaction.cpp
    ${SRC_DIR}/Update/OnlineUpdateCatalog.cpp
    ${SRC_DIR}/Resource/ResourceManifest.cpp
    ${SRC_DIR}/Resource/ModReleaseMetadata.cpp
    ${SRC_DIR}/Resource/SemanticVersion.cpp
    ${SRC_DIR}/File/StrictRelativeResourcePath.cpp
    ${SRC_DIR}/File/ini.cpp
)
target_compile_definitions(jxqy-resource-install-transaction-tests PRIVATE
    JXQY_RESOURCE_MANIFEST_BUFFER_ONLY)
add_test(NAME jxqy-resource-install-transaction-tests
    COMMAND jxqy-resource-install-transaction-tests)

add_executable(jxqy-https-download-tests
    ${SRC_DIR}/tests/test_https_download.cpp
    ${SRC_DIR}/Update/HttpsDownload.cpp
    ${SRC_DIR}/Update/OnlineUpdateCatalog.cpp
    ${SRC_DIR}/Resource/SemanticVersion.cpp
    ${SRC_DIR}/Resource/ModReleaseMetadata.cpp
    ${SRC_DIR}/File/StrictRelativeResourcePath.cpp
    ${SRC_DIR}/File/ini.cpp
)
if(WIN32)
    target_link_libraries(jxqy-https-download-tests PRIVATE winhttp)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-https-download-tests PRIVATE CURL::libcurl)
endif()
add_test(NAME jxqy-https-download-tests
    COMMAND jxqy-https-download-tests)

add_executable(jxqy-editor-run-launch-tests
    ${SRC_DIR}/tests/test_editor_run_preflight.cpp
    ${SRC_DIR}/Launch/GameLaunchArguments.cpp
    ${SRC_DIR}/Launch/EditorRunDescriptor.cpp
    ${SRC_DIR}/File/StrictRelativeResourcePath.cpp
)
add_test(NAME jxqy-editor-run-launch-tests
    COMMAND jxqy-editor-run-launch-tests)

add_executable(jxqy-newgame-automation-probe-tests
    ${SRC_DIR}/tests/test_newgame_automation_process.cpp
    ${SRC_DIR}/Launch/GameLaunchArguments.cpp
)
add_test(NAME jxqy-newgame-automation-process-tests
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_newgame_automation_process.py"
        --probe "$<TARGET_FILE:jxqy-newgame-automation-probe-tests>"
        --game "$<TARGET_FILE:${PROJECT_NAME}>"
        --repo "${CMAKE_CURRENT_SOURCE_DIR}"
        --assets "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        --resource-id XJXQY_TEST_MOD
)
set_tests_properties(jxqy-newgame-automation-process-tests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RUN_SERIAL TRUE
    SKIP_RETURN_CODE 77
    TIMEOUT 3000
)

add_executable(jxqy-editor-run-runtime-session-tests
    ${SRC_DIR}/tests/test_editor_run_runtime_session.cpp
    ${SRC_DIR}/Launch/EditorRunRuntimeSession.cpp
    ${SRC_DIR}/Launch/EditorRunDescriptor.cpp
    ${SRC_DIR}/File/RootedResourceReader.cpp
    ${SRC_DIR}/File/StrictRelativeResourcePath.cpp
)
add_test(NAME jxqy-editor-run-runtime-session-tests
    COMMAND jxqy-editor-run-runtime-session-tests)

add_executable(jxqy-editor-run-resource-routing-tests
    ${SRC_DIR}/tests/test_editor_run_resource_routing.cpp
    ${SRC_DIR}/Launch/EditorRunResourceRouting.cpp
    ${SRC_DIR}/Resource/ResourceCatalog.cpp
    ${SRC_DIR}/Resource/ResourceManifest.cpp
    ${JXQY_MOD_RELEASE_METADATA_SOURCES}
    ${SRC_DIR}/File/RootedResourceReader.cpp
    ${SRC_DIR}/File/INIReader.cpp
    ${SRC_DIR}/File/ini.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)
if(WIN32)
    target_link_libraries(jxqy-editor-run-resource-routing-tests
        SDL3_lib)
    add_custom_command(
        TARGET jxqy-editor-run-resource-routing-tests
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:jxqy-editor-run-resource-routing-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-editor-run-resource-routing-tests SDL3)
endif()
add_test(NAME jxqy-editor-run-resource-routing-tests
    COMMAND jxqy-editor-run-resource-routing-tests)

add_executable(jxqy-resource-catalog-supplemental-tests
    ${SRC_DIR}/tests/test_resource_catalog_supplemental.cpp
    ${SRC_DIR}/Resource/ResourceCatalog.cpp
    ${SRC_DIR}/Resource/ResourceManifest.cpp
    ${JXQY_MOD_RELEASE_METADATA_SOURCES}
    ${SRC_DIR}/File/RootedResourceReader.cpp
    ${SRC_DIR}/File/INIReader.cpp
    ${SRC_DIR}/File/ini.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)
if(WIN32)
    target_link_libraries(jxqy-resource-catalog-supplemental-tests
        SDL3_lib)
    add_custom_command(
        TARGET jxqy-resource-catalog-supplemental-tests
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:jxqy-resource-catalog-supplemental-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-resource-catalog-supplemental-tests SDL3)
endif()
add_test(NAME jxqy-resource-catalog-supplemental-tests
    COMMAND jxqy-resource-catalog-supplemental-tests)

add_executable(jxqy-editor-run-resource-install-tests
    ${SRC_DIR}/tests/test_editor_run_resource_install.cpp
    ${SRC_DIR}/Platform/AndroidExternalStorage.cpp
    ${SRC_DIR}/Resource/ResourceCatalog.cpp
    ${SRC_DIR}/Resource/ResourceManager.cpp
    ${SRC_DIR}/Resource/ResourceManifest.cpp
    ${SRC_DIR}/Update/OnlineUpdateCatalog.cpp
    ${SRC_DIR}/Update/ResourceInstallTransaction.cpp
    ${JXQY_MOD_RELEASE_METADATA_SOURCES}
    ${SRC_DIR}/File/RootedResourceReader.cpp
    ${SRC_DIR}/File/INIReader.cpp
    ${SRC_DIR}/File/ini.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)
if(WIN32)
    target_link_libraries(jxqy-editor-run-resource-install-tests
        SDL3_lib)
    add_custom_command(
        TARGET jxqy-editor-run-resource-install-tests
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:jxqy-editor-run-resource-install-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-editor-run-resource-install-tests SDL3)
endif()
add_test(NAME jxqy-editor-run-resource-install-tests
    COMMAND jxqy-editor-run-resource-install-tests)

add_executable(jxqy-resource-manager-install-transaction-tests
    ${SRC_DIR}/tests/test_resource_manager_install_transaction.cpp
    ${SRC_DIR}/Platform/AndroidExternalStorage.cpp
    ${SRC_DIR}/Resource/ResourceCatalog.cpp
    ${SRC_DIR}/Resource/ResourceManager.cpp
    ${SRC_DIR}/Resource/ResourceManifest.cpp
    ${SRC_DIR}/Update/OnlineUpdateCatalog.cpp
    ${SRC_DIR}/Update/ResourceInstallTransaction.cpp
    ${JXQY_MOD_RELEASE_METADATA_SOURCES}
    ${SRC_DIR}/File/RootedResourceReader.cpp
    ${SRC_DIR}/File/INIReader.cpp
    ${SRC_DIR}/File/ini.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)
if(WIN32)
    target_link_libraries(jxqy-resource-manager-install-transaction-tests
        SDL3_lib)
    add_custom_command(
        TARGET jxqy-resource-manager-install-transaction-tests
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:jxqy-resource-manager-install-transaction-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-resource-manager-install-transaction-tests SDL3)
endif()
add_test(NAME jxqy-resource-manager-install-transaction-tests
    COMMAND jxqy-resource-manager-install-transaction-tests)

add_executable(jxqy-editor-run-scene-application-tests
    ${SRC_DIR}/tests/test_editor_run_scene_application.cpp
    ${SRC_DIR}/Launch/EditorRunSceneApplication.cpp
    ${SRC_DIR}/File/RootedResourceReader.cpp
    ${SRC_DIR}/File/StrictRelativeResourcePath.cpp
)
add_test(NAME jxqy-editor-run-scene-application-tests
    COMMAND jxqy-editor-run-scene-application-tests)

find_package(Threads REQUIRED)
add_executable(jxqy-editor-run-diagnostics-tests
    ${SRC_DIR}/tests/test_editor_run_diagnostics.cpp
    ${SRC_DIR}/Launch/EditorRunDiagnostics.cpp
)
target_link_libraries(jxqy-editor-run-diagnostics-tests
    Threads::Threads)
add_test(NAME jxqy-editor-run-diagnostics-tests
    COMMAND jxqy-editor-run-diagnostics-tests)

add_executable(jxqy-editor-run-runtime-trace-tests
    ${SRC_DIR}/tests/test_editor_run_runtime_trace.cpp
    ${SRC_DIR}/Launch/EditorRunRuntimeTrace.cpp
    ${SRC_DIR}/Launch/EditorRunRuntimeTraceWriter.cpp
    ${SRC_DIR}/File/StrictRelativeResourcePath.cpp
)
target_link_libraries(jxqy-editor-run-runtime-trace-tests
    Threads::Threads)
add_test(NAME jxqy-editor-run-runtime-trace-tests
    COMMAND jxqy-editor-run-runtime-trace-tests)

add_executable(jxqy-editor-run-orchestration-tests
    ${SRC_DIR}/tests/test_editor_run_orchestration.cpp
    ${SRC_DIR}/Launch/EditorRunOrchestration.cpp
    ${SRC_DIR}/Launch/EditorRunDiagnostics.cpp
    ${SRC_DIR}/Launch/EditorRunRuntimeTrace.cpp
    ${SRC_DIR}/Launch/EditorRunRuntimeTraceWriter.cpp
    ${SRC_DIR}/File/StrictRelativeResourcePath.cpp
)
target_link_libraries(jxqy-editor-run-orchestration-tests
    Threads::Threads)
add_test(NAME jxqy-editor-run-orchestration-tests
    COMMAND jxqy-editor-run-orchestration-tests)

option(JXQY_ENABLE_PYTHON_CONTRACT_TESTS
    "Register Python source and runtime contract tests" ON)
if(JXQY_ENABLE_PYTHON_CONTRACT_TESTS)
    add_test(NAME jxqy-scaffold-test-mod-contracts
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_scaffold_test_mod.py")
    add_test(NAME jxqy-run-mod-scenario-smoke-contracts
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_run_mod_scenario_smoke.py")
    add_test(NAME jxqy-platform-project-source-contracts
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_platform_project_sources.py")
    add_test(NAME jxqy-menu-input-catalog-contracts
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_menu_input_catalog.py")
    add_test(NAME jxqy-menu-input-catalog-unit-tests
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_check_menu_input_catalog.py")
    set_tests_properties(
        jxqy-scaffold-test-mod-contracts
        jxqy-run-mod-scenario-smoke-contracts
        jxqy-platform-project-source-contracts
        jxqy-menu-input-catalog-unit-tests
        PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RUN_SERIAL TRUE
            TIMEOUT 120
    )
    set_tests_properties(jxqy-menu-input-catalog-contracts PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RUN_SERIAL TRUE
        TIMEOUT 1200
    )
endif()

add_test(NAME jxqy-trap-tests COMMAND jxqy-trap-tests)

add_executable(jxqy-npc-action-bounce-tests
    ${SRC_DIR}/tests/test_npc_action_bounce.cpp
)

add_test(NAME jxqy-npc-action-bounce-tests COMMAND jxqy-npc-action-bounce-tests)

add_executable(jxqy-npc-action-death-tests
    ${SRC_DIR}/tests/test_npc_action_death.cpp
)

add_test(NAME jxqy-npc-action-death-tests COMMAND jxqy-npc-action-death-tests)

add_executable(jxqy-npc-action-shadow-selection-tests
    ${SRC_DIR}/tests/test_npc_action_shadow_selection.cpp
)

add_test(NAME jxqy-npc-action-shadow-selection-tests COMMAND jxqy-npc-action-shadow-selection-tests)

add_executable(jxqy-npc-action-magic-timing-tests
    ${SRC_DIR}/tests/test_npc_action_magic_timing.cpp
)

add_test(NAME jxqy-npc-action-magic-timing-tests COMMAND jxqy-npc-action-magic-timing-tests)

add_executable(jxqy-camera-vibration-tests
    ${SRC_DIR}/tests/test_camera_vibration.cpp
)

add_test(NAME jxqy-camera-vibration-tests COMMAND jxqy-camera-vibration-tests)

add_executable(jxqy-object-runtime-tests
    ${SRC_DIR}/tests/test_object_runtime.cpp
)

add_test(NAME jxqy-object-runtime-tests COMMAND jxqy-object-runtime-tests)

add_executable(jxqy-media-path-resolver-tests
    ${SRC_DIR}/tests/test_media_path_resolver.cpp
    ${SRC_DIR}/Game/Data/MediaPathResolver.cpp
    ${SRC_DIR}/Image/PicDecoder.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)

if(WIN32)
    target_link_libraries(jxqy-media-path-resolver-tests SDL3_lib)
    add_custom_command(TARGET jxqy-media-path-resolver-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:jxqy-media-path-resolver-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-media-path-resolver-tests SDL3)
endif()

add_test(NAME jxqy-media-path-resolver-tests COMMAND jxqy-media-path-resolver-tests)

set(JXQY_NPC_TARGETING_RUNTIME_SOURCES ${SOURCE_FILES})
list(REMOVE_ITEM JXQY_NPC_TARGETING_RUNTIME_SOURCES ${SRC_DIR}/main.cpp)

add_executable(jxqy-npc-targeting-tests
    ${SRC_DIR}/tests/test_npc_targeting.cpp
    ${SRC_DIR}/tests/test_npc_runtime_persistence.cpp
    ${SRC_DIR}/tests/test_effect_runtime_persistence.cpp
    ${SRC_DIR}/tests/test_gamble_menu_runtime.cpp
    ${SRC_DIR}/tests/test_magic_derived_runtime.cpp
    ${SRC_DIR}/tests/test_object_animation_runtime.cpp
    ${SRC_DIR}/tests/test_media_runtime.cpp
    ${SRC_DIR}/tests/test_core_lifecycle.cpp
	${SRC_DIR}/tests/test_ui_focus.cpp
	${SRC_DIR}/tests/test_map_thumbnail_controller.cpp
	${SRC_DIR}/tests/test_partner_equipment_transfer.cpp
	${SRC_DIR}/tests/test_world_interaction_runtime.cpp
	${SRC_DIR}/tests/test_gamepad_world_runtime.cpp
	${SRC_DIR}/tests/test_gamepad_essential_ui.cpp
	${SRC_DIR}/tests/test_gamepad_rpg_menu_actions.cpp
	${SRC_DIR}/tests/test_gamepad_surface_contract.cpp
	${SRC_DIR}/tests/test_mobile_external_input_runtime.cpp
	${SRC_DIR}/tests/test_script_engine_runtime.cpp
	${SRC_DIR}/tests/test_editor_run_scene_runtime.cpp
	${SRC_DIR}/tests/test_map_v3_runtime.cpp
    ${JXQY_NPC_TARGETING_RUNTIME_SOURCES}
)

if(JXQY_FORCE_MOBILE_UI)
	target_compile_definitions(jxqy-npc-targeting-tests PRIVATE
		JXQY_TEST_ANDROID_EXTERNAL_RESOURCE_UI)
endif()

if(WIN32)
    target_link_libraries(jxqy-npc-targeting-tests
        SDL3_lib
        SDL3_image_lib
        SDL3_ttf_lib
        SDL3_mixer_lib
        winhttp
        ${FFMPEG_LIBS}
    )
    add_custom_command(TARGET jxqy-npc-targeting-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_RUNTIME_DLL_DEPS}
            "$<TARGET_FILE_DIR:jxqy-npc-targeting-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-npc-targeting-tests
        ${JXQY_LINUX_MEDIA_LIBRARIES}
        CURL::libcurl
        Threads::Threads
        ${CMAKE_DL_LIBS}
    )
endif()

add_test(NAME jxqy-npc-targeting-tests COMMAND jxqy-npc-targeting-tests)
add_test(NAME jxqy-npc-runtime-persistence-tests COMMAND jxqy-npc-targeting-tests --npc-persistence)
add_test(NAME jxqy-effect-runtime-persistence-tests COMMAND jxqy-npc-targeting-tests --effect-persistence)
add_test(NAME jxqy-gamble-menu-runtime-tests COMMAND jxqy-npc-targeting-tests --gamble-menu)
add_test(NAME jxqy-magic-derived-runtime-tests COMMAND jxqy-npc-targeting-tests --magic-derived)
add_test(NAME jxqy-magic-experience-tests COMMAND jxqy-npc-targeting-tests --magic-experience)
add_test(NAME jxqy-object-animation-runtime-tests COMMAND jxqy-npc-targeting-tests --object-animation)
add_test(NAME jxqy-media-runtime-tests COMMAND jxqy-npc-targeting-tests --media-runtime)
add_test(NAME jxqy-core-lifecycle-tests COMMAND jxqy-npc-targeting-tests --core-lifecycle)
set_tests_properties(jxqy-core-lifecycle-tests PROPERTIES TIMEOUT 180)
add_test(NAME jxqy-ui-focus-tests COMMAND jxqy-npc-targeting-tests --ui-focus)
add_test(NAME jxqy-map-thumbnail-controller-tests
	COMMAND jxqy-npc-targeting-tests --map-thumbnail-controller)
add_test(NAME jxqy-partner-equipment-transfer-tests
	COMMAND jxqy-npc-targeting-tests --partner-equipment-transfer)
add_test(NAME jxqy-world-interaction-tests
	COMMAND jxqy-npc-targeting-tests --world-interaction-runtime)
add_test(NAME jxqy-gamepad-world-runtime-tests
	COMMAND jxqy-npc-targeting-tests --gamepad-world-runtime)
add_test(NAME jxqy-gamepad-essential-ui-tests
	COMMAND jxqy-npc-targeting-tests --gamepad-essential-ui)
add_test(NAME jxqy-gamepad-rpg-menu-action-tests
	COMMAND jxqy-npc-targeting-tests --gamepad-rpg-menu-actions)
add_test(NAME jxqy-gamepad-surface-contract-tests
	COMMAND jxqy-npc-targeting-tests --gamepad-surface-contract)
if(JXQY_FORCE_MOBILE_UI)
	add_test(NAME jxqy-mobile-external-input-runtime-tests
		COMMAND jxqy-npc-targeting-tests --mobile-external-input-runtime)
endif()
add_test(NAME jxqy-script-engine-runtime-tests COMMAND jxqy-npc-targeting-tests --script-engine-runtime)
add_test(NAME jxqy-editor-run-scene-runtime-tests
	COMMAND jxqy-npc-targeting-tests --editor-run-scene-runtime)
add_test(NAME jxqy-map-v3-runtime-tests COMMAND jxqy-npc-targeting-tests --map-v3-load)

add_executable(jxqy-npc-magic-replacement-tests
    ${SRC_DIR}/tests/test_npc_magic_replacement.cpp
)

add_test(NAME jxqy-npc-magic-replacement-tests COMMAND jxqy-npc-magic-replacement-tests)

add_executable(jxqy-magic-damage-channel-tests
    ${SRC_DIR}/tests/test_magic_damage_channels.cpp
)

add_test(NAME jxqy-magic-damage-channel-tests COMMAND jxqy-magic-damage-channel-tests)

add_executable(jxqy-magic-hit-rate-tests
    ${SRC_DIR}/tests/test_magic_hit_rate.cpp
)

add_test(NAME jxqy-magic-hit-rate-tests COMMAND jxqy-magic-hit-rate-tests)

add_executable(jxqy-magic-begin-position-tests
    ${SRC_DIR}/tests/test_magic_begin_position.cpp
    ${SRC_DIR}/Game/Data/MagicBeginPosition.cpp
)

add_test(NAME jxqy-magic-begin-position-tests COMMAND jxqy-magic-begin-position-tests)

add_executable(jxqy-magic-region-shape-tests
    ${SRC_DIR}/tests/test_magic_region_shape.cpp
)

add_test(NAME jxqy-magic-region-shape-tests COMMAND jxqy-magic-region-shape-tests)

add_executable(jxqy-magic-transport-tests
    ${SRC_DIR}/tests/test_magic_transport.cpp
    ${SRC_DIR}/Game/Data/MagicTransport.cpp
)

add_test(NAME jxqy-magic-transport-tests COMMAND jxqy-magic-transport-tests)

add_executable(jxqy-magic-control-tests
    ${SRC_DIR}/tests/test_magic_control.cpp
    ${SRC_DIR}/Game/Data/MagicControl.cpp
)

add_test(NAME jxqy-magic-control-tests COMMAND jxqy-magic-control-tests)

add_executable(jxqy-time-stop-update-gate-tests
    ${SRC_DIR}/tests/test_time_stop_update_gate.cpp
    ${SRC_DIR}/Element/Element.cpp
)

add_test(NAME jxqy-time-stop-update-gate-tests COMMAND jxqy-time-stop-update-gate-tests)

add_executable(jxqy-choose-multiple-selection-tests
    ${SRC_DIR}/tests/test_choose_multiple_selection.cpp
    ${SRC_DIR}/Game/Menu/ChooseMultipleSelection.cpp
)

add_test(NAME jxqy-choose-multiple-selection-tests COMMAND jxqy-choose-multiple-selection-tests)

add_executable(jxqy-controller-transfer-coordinator-tests
	${SRC_DIR}/tests/test_controller_transfer_coordinator.cpp
	${SRC_DIR}/Game/Menu/ControllerTransferCoordinator.cpp
)

add_test(NAME jxqy-controller-transfer-coordinator-tests
	COMMAND jxqy-controller-transfer-coordinator-tests)

add_executable(jxqy-input-action-tests
	${SRC_DIR}/tests/test_input_action.cpp
	${SRC_DIR}/Input/PhysicalInputManager.cpp
)

add_executable(jxqy-sdl-gamepad-tests
	${SRC_DIR}/tests/test_sdl_gamepad.cpp
	${SRC_DIR}/Input/PhysicalInputManager.cpp
)

add_executable(jxqy-desktop-cursor-dpi-policy-tests
	${SRC_DIR}/tests/test_desktop_cursor_dpi_policy.cpp
)

if(WIN32)
	target_link_libraries(jxqy-input-action-tests SDL3_lib)
	target_link_libraries(jxqy-sdl-gamepad-tests SDL3_lib)
	target_link_libraries(jxqy-desktop-cursor-dpi-policy-tests SDL3_lib)
	foreach(_SDL_INPUT_TEST_TARGET
			jxqy-input-action-tests
			jxqy-sdl-gamepad-tests
			jxqy-desktop-cursor-dpi-policy-tests)
		add_custom_command(TARGET ${_SDL_INPUT_TEST_TARGET} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${SDL3_DLL}"
				"$<TARGET_FILE_DIR:${_SDL_INPUT_TEST_TARGET}>"
			VERBATIM)
	endforeach()
elseif(UNIX AND NOT APPLE)
	target_link_libraries(jxqy-input-action-tests SDL3)
	target_link_libraries(jxqy-sdl-gamepad-tests SDL3)
	target_link_libraries(jxqy-desktop-cursor-dpi-policy-tests SDL3)
endif()

add_test(NAME jxqy-input-action-tests COMMAND jxqy-input-action-tests)
add_test(NAME jxqy-sdl-gamepad-tests COMMAND jxqy-sdl-gamepad-tests)
add_test(NAME jxqy-desktop-cursor-dpi-policy-tests
	COMMAND jxqy-desktop-cursor-dpi-policy-tests)

add_executable(jxqy-choose-menu-layout-tests
    ${SRC_DIR}/tests/test_choose_menu_layout.cpp
    ${SRC_DIR}/Game/Menu/ChooseMenuLayout.cpp
    ${SRC_DIR}/Component/TextLayout.cpp
)

add_test(NAME jxqy-choose-menu-layout-tests COMMAND jxqy-choose-menu-layout-tests)

add_executable(jxqy-logical-resolution-policy-tests
    ${SRC_DIR}/tests/test_logical_resolution_policy.cpp
)

add_test(NAME jxqy-logical-resolution-policy-tests COMMAND jxqy-logical-resolution-policy-tests)

add_executable(jxqy-element-pointer-click-policy-tests
    ${SRC_DIR}/tests/test_element_pointer_click_policy.cpp
)

add_test(NAME jxqy-element-pointer-click-policy-tests COMMAND jxqy-element-pointer-click-policy-tests)

add_executable(jxqy-flat-component-policy-tests
    ${SRC_DIR}/tests/test_flat_component_policy.cpp
)

add_test(NAME jxqy-flat-component-policy-tests COMMAND jxqy-flat-component-policy-tests)

add_executable(jxqy-gamble-menu-policy-tests
    ${SRC_DIR}/tests/test_gamble_menu_policy.cpp
)

add_test(NAME jxqy-gamble-menu-policy-tests COMMAND jxqy-gamble-menu-policy-tests)

add_executable(jxqy-fish-game-state-tests
    ${SRC_DIR}/tests/test_fish_game_state.cpp
    ${SRC_DIR}/Game/Menu/FishGameState.cpp
)

add_test(NAME jxqy-fish-game-state-tests COMMAND jxqy-fish-game-state-tests)

add_executable(jxqy-dice-gamble-state-tests
    ${SRC_DIR}/tests/test_dice_gamble_state.cpp
    ${SRC_DIR}/Game/Menu/DiceGambleState.cpp
)

add_test(NAME jxqy-dice-gamble-state-tests COMMAND jxqy-dice-gamble-state-tests)

add_executable(jxqy-mobile-touch-interaction-tests
    ${SRC_DIR}/tests/test_mobile_touch_interaction.cpp
)

add_test(NAME jxqy-mobile-touch-interaction-tests COMMAND jxqy-mobile-touch-interaction-tests)

add_executable(jxqy-script-runtime-state-tests
    ${SRC_DIR}/tests/test_script_runtime_state.cpp
    ${SRC_DIR}/Game/GameManager/ScriptRuntimeState.cpp
    ${SRC_DIR}/File/INIReader.cpp
    ${SRC_DIR}/File/ini.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)

if(WIN32)
    target_link_libraries(jxqy-script-runtime-state-tests SDL3_lib)
    add_custom_command(TARGET jxqy-script-runtime-state-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:jxqy-script-runtime-state-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-script-runtime-state-tests SDL3)
endif()

add_test(NAME jxqy-script-runtime-state-tests COMMAND jxqy-script-runtime-state-tests)

add_executable(jxqy-buy-sell-inventory-tests
    ${SRC_DIR}/tests/test_buy_sell_inventory.cpp
    ${SRC_DIR}/Game/Data/BuySellInventory.cpp
    ${SRC_DIR}/File/INIReader.cpp
    ${SRC_DIR}/File/ini.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)

if(WIN32)
    target_link_libraries(jxqy-buy-sell-inventory-tests SDL3_lib)
    add_custom_command(TARGET jxqy-buy-sell-inventory-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:jxqy-buy-sell-inventory-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-buy-sell-inventory-tests SDL3)
endif()

add_test(NAME jxqy-buy-sell-inventory-tests COMMAND jxqy-buy-sell-inventory-tests)

add_executable(jxqy-memo-persistence-tests
    ${SRC_DIR}/tests/test_memo_persistence.cpp
    ${SRC_DIR}/Game/Data/MemoPersistence.cpp
    ${SRC_DIR}/File/INIReader.cpp
    ${SRC_DIR}/File/ini.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)

if(WIN32)
    target_link_libraries(jxqy-memo-persistence-tests SDL3_lib)
    add_custom_command(TARGET jxqy-memo-persistence-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:jxqy-memo-persistence-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-memo-persistence-tests SDL3)
endif()

add_test(NAME jxqy-memo-persistence-tests COMMAND jxqy-memo-persistence-tests)

add_executable(jxqy-goods-pricing-tests
    ${SRC_DIR}/tests/test_goods_pricing.cpp
    ${SRC_DIR}/Game/Data/Goods.cpp
    ${SRC_DIR}/Game/GameManager/SaveFileManager.cpp
    ${SRC_DIR}/Game/GameManager/SaveGeneration.cpp
    ${SRC_DIR}/File/INIReader.cpp
    ${SRC_DIR}/File/ini.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)

if(WIN32)
    target_link_libraries(jxqy-goods-pricing-tests SDL3_lib)
    add_custom_command(TARGET jxqy-goods-pricing-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:jxqy-goods-pricing-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-goods-pricing-tests SDL3)
endif()

add_test(NAME jxqy-goods-pricing-tests COMMAND jxqy-goods-pricing-tests)

add_executable(jxqy-file-tests
    ${SRC_DIR}/tests/test_file_resource_roots.cpp
    ${SRC_DIR}/Launch/EditorRunDiagnostics.cpp
    ${SRC_DIR}/Launch/EditorRunDiagnosticsFile.cpp
    ${SRC_DIR}/Launch/EditorRunRuntimeTrace.cpp
    ${SRC_DIR}/Launch/EditorRunRuntimeTraceFile.cpp
    ${SRC_DIR}/Launch/EditorRunRuntimeTraceWriter.cpp
    ${SRC_DIR}/Platform/AndroidExternalStorage.cpp
    ${SRC_DIR}/Resource/ResourceCatalog.cpp
    ${SRC_DIR}/Resource/ResourceManager.cpp
    ${SRC_DIR}/Resource/ResourceManifest.cpp
    ${SRC_DIR}/Update/OnlineUpdateCatalog.cpp
    ${SRC_DIR}/Update/ResourceInstallTransaction.cpp
    ${JXQY_MOD_RELEASE_METADATA_SOURCES}
    ${SRC_DIR}/Game/GameManager/SaveFileManager.cpp
    ${SRC_DIR}/Game/GameManager/SaveGeneration.cpp
    ${SRC_DIR}/File/INIReader.cpp
    ${SRC_DIR}/File/ini.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/RootedResourceReader.cpp
    ${SRC_DIR}/File/StrictRelativeResourcePath.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)
target_link_libraries(jxqy-file-tests Threads::Threads)

if(WIN32)
    target_link_libraries(jxqy-file-tests SDL3_lib)
    add_custom_command(TARGET jxqy-file-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:jxqy-file-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-file-tests SDL3)
endif()

add_test(NAME jxqy-file-tests COMMAND jxqy-file-tests)
if(JXQY_FORCE_MOBILE_UI)
    add_test(
        NAME jxqy-mobile-resource-policy-tests
        COMMAND jxqy-file-tests --resource-manager-policy-only
    )
endif()

add_executable(jxqy-exclusive-loading-runner-tests
    ${SRC_DIR}/tests/test_exclusive_loading_runner.cpp
    ${SRC_DIR}/Game/Loading/ExclusiveLoadingRunner.cpp
)
target_compile_definitions(jxqy-exclusive-loading-runner-tests PRIVATE
    JXQY_EXCLUSIVE_LOADING_RUNNER_STANDALONE=1
)
target_link_libraries(jxqy-exclusive-loading-runner-tests Threads::Threads)
add_test(NAME jxqy-exclusive-loading-runner-tests
    COMMAND jxqy-exclusive-loading-runner-tests)

add_executable(jxqy-save-generation-tests
    ${SRC_DIR}/tests/test_save_generation.cpp
    ${SRC_DIR}/Game/GameManager/SaveGeneration.cpp
    ${SRC_DIR}/Game/GameManager/SaveFileManager.cpp
    ${SRC_DIR}/File/INIReader.cpp
    ${SRC_DIR}/File/ini.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/StrictRelativeResourcePath.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)
target_compile_definitions(jxqy-save-generation-tests PRIVATE
    JXQY_SAVE_GENERATION_STANDALONE=1
)
target_link_libraries(jxqy-save-generation-tests Threads::Threads)

if(WIN32)
    target_link_libraries(jxqy-save-generation-tests SDL3_lib)
    add_custom_command(TARGET jxqy-save-generation-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:jxqy-save-generation-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-save-generation-tests SDL3)
endif()

add_test(NAME jxqy-save-generation-tests
    COMMAND jxqy-save-generation-tests)

add_executable(jxqy-game-feature-profile-tests
    ${SRC_DIR}/tests/test_game_feature_profile.cpp
    ${SRC_DIR}/Game/Data/Global.cpp
    ${SRC_DIR}/Platform/AndroidExternalStorage.cpp
    ${SRC_DIR}/Resource/ResourceCatalog.cpp
    ${SRC_DIR}/Resource/ResourceManager.cpp
    ${SRC_DIR}/Resource/ResourceManifest.cpp
    ${SRC_DIR}/Update/OnlineUpdateCatalog.cpp
    ${SRC_DIR}/Update/ResourceInstallTransaction.cpp
    ${JXQY_MOD_RELEASE_METADATA_SOURCES}
    ${SRC_DIR}/File/RootedResourceReader.cpp
    ${SRC_DIR}/File/INIReader.cpp
    ${SRC_DIR}/File/ini.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)

if(WIN32)
    target_link_libraries(jxqy-game-feature-profile-tests SDL3_lib)
    add_custom_command(TARGET jxqy-game-feature-profile-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:jxqy-game-feature-profile-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-game-feature-profile-tests SDL3)
endif()

add_test(NAME jxqy-game-feature-profile-tests COMMAND jxqy-game-feature-profile-tests)

add_executable(jxqy-magic-region-file-tests
    ${SRC_DIR}/tests/test_magic_region_file.cpp
    ${SRC_DIR}/Game/Data/MagicRegionFile.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)

if(WIN32)
    target_link_libraries(jxqy-magic-region-file-tests SDL3_lib)
    add_custom_command(TARGET jxqy-magic-region-file-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "$<TARGET_FILE_DIR:jxqy-magic-region-file-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-magic-region-file-tests SDL3)
endif()

add_test(NAME jxqy-magic-region-file-tests COMMAND jxqy-magic-region-file-tests)

add_executable(jxqy-resource-smoke-tests
    ${SRC_DIR}/tests/test_resource_smoke.cpp
    ${SRC_DIR}/Game/Data/MediaPathResolver.cpp
    ${SRC_DIR}/Platform/AndroidExternalStorage.cpp
    ${SRC_DIR}/Resource/ResourceCatalog.cpp
    ${SRC_DIR}/Resource/ResourceManager.cpp
    ${SRC_DIR}/Resource/ResourceManifest.cpp
    ${SRC_DIR}/Update/OnlineUpdateCatalog.cpp
    ${SRC_DIR}/Update/ResourceInstallTransaction.cpp
    ${JXQY_MOD_RELEASE_METADATA_SOURCES}
    ${SRC_DIR}/Resource/ModReleaseAssets.cpp
    ${SRC_DIR}/File/INIReader.cpp
    ${SRC_DIR}/File/ini.cpp
    ${SRC_DIR}/File/File.cpp
    ${SRC_DIR}/File/RootedResourceReader.cpp
    ${SRC_DIR}/File/log.cpp
    ${SRC_DIR}/libconvert/libconvert.cpp
)

if(WIN32)
    target_link_libraries(jxqy-resource-smoke-tests
        SDL3_lib
        SDL3_image_lib
    )
    add_custom_command(TARGET jxqy-resource-smoke-tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_DLL}"
            "${SDL3_IMAGE_DLL}"
            "$<TARGET_FILE_DIR:jxqy-resource-smoke-tests>"
        VERBATIM)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(jxqy-resource-smoke-tests
        SDL3
        SDL3_image
    )
endif()

add_test(NAME jxqy-resource-smoke-tests COMMAND jxqy-resource-smoke-tests)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/resources.ini")
    add_test(NAME jxqy-real-assets-gamble-resource-tests
        COMMAND jxqy-resource-smoke-tests
            --assets "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            --require-gamble-fallback-packs)
    add_test(NAME jxqy-relative-assets-release-tests
        COMMAND jxqy-resource-smoke-tests
            --assets assets)
    set_tests_properties(jxqy-relative-assets-release-tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
endif()

# Keep every compiled test executable inside its own target directory under the
# current CMake build tree. Besides isolating desktop/mobile build trees, the
# per-target directory prevents parallel POST_BUILD dependency copies from
# racing on a shared SDL DLL.
get_property(JXQY_DIRECTORY_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(JXQY_TARGET IN LISTS JXQY_DIRECTORY_TARGETS)
    if(JXQY_TARGET MATCHES "-tests$")
        set_target_properties(${JXQY_TARGET} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-bin/${JXQY_TARGET}"
            PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-bin/${JXQY_TARGET}"
        )
    endif()

    # Every executable compiled with MSVC AddressSanitizer loads the dynamic
    # runtime before entering main(). Deploy it beside each executable instead
    # of limiting the copy to the game and the full-runtime test target; a
    # missing DLL otherwise becomes a blocking Windows error dialog in CTest.
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
