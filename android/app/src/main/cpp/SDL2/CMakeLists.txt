# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.10)
project(SDL2)

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

find_library(
        OpenSLES
        OpenSLES
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/include
                    ${ANDROID_NDK}/sources/android/cpufeatures
                )

add_definitions(-DGL_GLEXT_PROTOTYPES -DANDROID -DSDL_HIDAPI_DISABLED)

# 获取包含的源文件
file(GLOB SOURCE_FILES
        "${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/audio/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/audio/android/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/audio/dummy/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/audio/aaudio/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/audio/openslES/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/atomic/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/core/android/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/cpuinfo/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/dynapi/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/events/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/file/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/haptic/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/haptic/android/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/hidapi/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/hidapi/android/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/joystick/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/joystick/android/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/joystick/hidapi/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/joystick/virtual/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/loadso/dlopen/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/locale/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/locale/android/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/misc/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/misc/android/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/power/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/power/android/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/filesystem/android/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/sensor/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/sensor/android/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/*/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/stdlib/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/thread/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/thread/pthread/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/timer/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/timer/unix/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/android/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/yuv2rgb/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/*.c"
)


# 添加库
add_library( # Sets the name of the library.
        SDL2

        # Sets the library as a shared library.
        SHARED

        ${SOURCE_FILES}

#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/core/android/SDL_android.c
#        # Provides a relative path to your source file(s).
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/opengles/SDL_render_gles.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/opengles2/SDL_shaders_gles2.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/opengles2/SDL_render_gles2.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/opengl/SDL_shaders_gl.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/opengl/SDL_render_gl.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/SDL_render.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/software/SDL_blendline.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/software/SDL_drawline.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/software/SDL_blendfillrect.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/software/SDL_blendpoint.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/software/SDL_rotate.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/software/SDL_drawpoint.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/software/SDL_render_sw.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/SDL_yuv_sw.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/SDL_d3dmath.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/psp/SDL_render_psp.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/render/SDL_yuv_mmx.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/audio/SDL_mixer.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/audio/SDL_audiocvt.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/audio/SDL_wave.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/audio/dummy/SDL_dummyaudio.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/audio/SDL_audiodev.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/audio/android/SDL_androidaudio.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/audio/paudio/SDL_paudio.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/audio/SDL_audiotypecvt.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/audio/SDL_audio.c
#        #        ../testsdl/src/core/android/SDL_android.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/cpuinfo/SDL_cpuinfo.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/file/SDL_rwops.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/timer/unix/SDL_systimer.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/timer/SDL_timer.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_assert.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_crc32.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_harness.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_md5.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_fuzzer.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_random.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_log.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_imagePrimitives.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_imagePrimitivesBlend.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_compare.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_imageFace.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_imageBlit.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_common.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_font.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/test/SDL_test_imageBlitBlend.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/events/SDL_dropevents.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/events/SDL_mouse.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/events/SDL_clipboardevents.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/events/SDL_windowevents.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/events/SDL_keyboard.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/events/SDL_gesture.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/events/SDL_quit.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/events/SDL_events.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/events/SDL_touch.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/thread/SDL_thread.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/thread/pthread/SDL_syscond.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/thread/pthread/SDL_systhread.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/thread/pthread/SDL_sysmutex.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/thread/pthread/SDL_syssem.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/thread/pthread/SDL_systls.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/haptic/SDL_haptic.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/haptic/dummy/SDL_syshaptic.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_video.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_blit_0.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_bmp.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_blit_slow.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_egl.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_blit_1.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_blit.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_RLEaccel.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_stretch.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_blit_A.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_pixels.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_blit_N.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_fillrect.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/android/SDL_androidevents.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/android/SDL_androidtouch.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/android/SDL_androidwindow.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/android/SDL_androidmessagebox.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/android/SDL_androidclipboard.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/android/SDL_androidkeyboard.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/android/SDL_androidvideo.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/android/SDL_androidmouse.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/android/SDL_androidgl.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_blit_copy.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_clipboard.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_blit_auto.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_rect.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_shape.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/video/SDL_surface.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/loadso/dlopen/SDL_sysloadso.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/dynapi/SDL_dynapi.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/SDL_error.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/SDL_assert.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/stdlib/SDL_stdlib.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/stdlib/SDL_malloc.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/stdlib/SDL_string.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/stdlib/SDL_getenv.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/stdlib/SDL_iconv.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/stdlib/SDL_qsort.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/SDL.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/power/android/SDL_syspower.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/power/SDL_power.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/atomic/SDL_spinlock.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/atomic/SDL_atomic.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/joystick/SDL_gamecontroller.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/joystick/SDL_joystick.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/joystick/android/SDL_sysjoystick.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/filesystem/android/SDL_sysfilesystem.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/SDL_log.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/SDL_hints.c
#
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/s_sin.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/s_cos.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/s_copysign.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/s_fabs.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/k_rem_pio2.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/k_sin.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/s_atan.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/k_cos.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/s_scalbn.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/e_pow.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/e_atan2.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/s_tan.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/e_rem_pio2.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/e_log.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/e_sqrt.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/s_floor.c
#        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../3rd/SDL2/android/src/libm/k_tan.c

        )


# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.



# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
        SDL2
        GLESv1_CM
        GLESv2
        OpenSLES
        android
        log
        # Links the target library to the log library
        # included in the NDK.
        #${log-lib}
)
