#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd -- "${script_dir}/.." && pwd)"

# shellcheck source=dependencies.lock
source "${script_dir}/dependencies.lock"

dependency_root="${repository_root}/ThirdParty/devel/linux/x86_64"
download_cache="${repository_root}/build/linux-dependencies/downloads"
dependency_set_id="SDL3=${SDL3_VERSION};SDL3_image=${SDL3_IMAGE_VERSION};SDL3_ttf=${SDL3_TTF_VERSION};SDL3_mixer=${SDL3_MIXER_VERSION};FreeType=${FREETYPE_VERSION};FFmpeg=${FFMPEG_VERSION}"

required_files=(
    .dependency-set
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
    licenses/README.md
    licenses/SDL3.txt
    licenses/SDL3_image.txt
    licenses/SDL3_ttf.txt
    licenses/SDL3_mixer.txt
    licenses/FreeType.txt
    licenses/FFmpeg-LICENSE.md
    licenses/FFmpeg-LGPL-2.1.txt
)

dependencies_are_ready()
{
    [[ -f "${dependency_root}/.dependency-set" ]] || return 1
    [[ "$(<"${dependency_root}/.dependency-set")" == "${dependency_set_id}" ]] || return 1

    local required_file
    for required_file in "${required_files[@]}"; do
        [[ -e "${dependency_root}/${required_file}" ]] || return 1
    done

    local link_path link_target
    while IFS= read -r -d '' link_path; do
        link_target="$(readlink "${link_path}")"
        [[ -n "${link_target}" && "${link_target}" != /* ]] || return 1
        [[ -e "$(dirname "${link_path}")/${link_target}" ]] || return 1
    done < <(find "${dependency_root}/lib" -type l -print0)

    return 0
}

force_build=false
case "${1:-}" in
    "")
        ;;
    --check)
        dependencies_are_ready
        exit $?
        ;;
    --force)
        force_build=true
        ;;
    *)
        echo "Usage: $0 [--check|--force]" >&2
        exit 2
        ;;
esac

if (( $# > 1 )); then
    echo "Usage: $0 [--check|--force]" >&2
    exit 2
fi

if [[ "${force_build}" == false ]] && dependencies_are_ready; then
    echo "Private Linux dependencies are already complete: ${dependency_root}"
    exit 0
fi

if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
    echo "This dependency set can only be built for Linux x86_64." >&2
    exit 1
fi

required_tools=(cmake gcc g++ ldd make patchelf tar sha256sum readelf strings strip)
for required_tool in "${required_tools[@]}"; do
    if ! command -v "${required_tool}" >/dev/null 2>&1; then
        echo "Required build tool was not found: ${required_tool}" >&2
        exit 1
    fi
done
if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
    echo "curl or wget is required to download dependency sources." >&2
    exit 1
fi

parallel_jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
if [[ ! "${parallel_jobs}" =~ ^[1-9][0-9]*$ ]]; then
    parallel_jobs=2
fi

mkdir -p "${download_cache}"
work_directory="$(mktemp -d "${TMPDIR:-/tmp}/jxqy-linux-dependencies.XXXXXX")"
trap 'rm -rf -- "${work_directory}"' EXIT

source_root="${work_directory}/source"
build_root="${work_directory}/build"
sdl_prefix="${work_directory}/sdl-prefix"
ffmpeg_install="${work_directory}/ffmpeg-install"
stage_root="${work_directory}/stage"
mkdir -p "${source_root}" "${build_root}" "${sdl_prefix}" \
    "${ffmpeg_install}" "${stage_root}/include" "${stage_root}/lib" \
    "${stage_root}/licenses"

download_source()
{
    local archive_name="$1"
    local source_url="$2"
    local expected_sha256="$3"
    local archive_path="${download_cache}/${archive_name}"
    local partial_path="${archive_path}.part.$$"

    if [[ -f "${archive_path}" ]] &&
        ! printf '%s  %s\n' "${expected_sha256}" "${archive_path}" |
            sha256sum --check --status; then
        echo "Discarding cached file with an invalid SHA256: ${archive_path}"
        rm -f -- "${archive_path}"
    fi

    if [[ ! -f "${archive_path}" ]]; then
        rm -f -- "${partial_path}"
        echo "Downloading ${archive_name}"
        if command -v curl >/dev/null 2>&1; then
            local download_succeeded=false
            local attempt
            for attempt in 1 2 3 4 5; do
                if curl -fL -o "${partial_path}" "${source_url}"; then
                    download_succeeded=true
                    break
                fi
                rm -f -- "${partial_path}"
                if (( attempt < 5 )); then
                    echo "Download failed; retrying ${archive_name} (${attempt}/5)." >&2
                    sleep $((attempt * 2))
                fi
            done
            if [[ "${download_succeeded}" != true ]]; then
                echo "Unable to download ${archive_name} after 5 attempts." >&2
                exit 1
            fi
        else
            wget -O "${partial_path}" "${source_url}"
        fi
        printf '%s  %s\n' "${expected_sha256}" "${partial_path}" |
            sha256sum --check --status
        mv -- "${partial_path}" "${archive_path}"
    fi

    printf '%s  %s\n' "${expected_sha256}" "${archive_path}" |
        sha256sum --check --status
}

download_source "SDL3-${SDL3_VERSION}.tar.gz" \
    "${SDL3_URL}" "${SDL3_SHA256}"
download_source "SDL3_image-${SDL3_IMAGE_VERSION}.tar.gz" \
    "${SDL3_IMAGE_URL}" "${SDL3_IMAGE_SHA256}"
download_source "SDL3_ttf-${SDL3_TTF_VERSION}.tar.gz" \
    "${SDL3_TTF_URL}" "${SDL3_TTF_SHA256}"
download_source "SDL3_mixer-${SDL3_MIXER_VERSION}.tar.gz" \
    "${SDL3_MIXER_URL}" "${SDL3_MIXER_SHA256}"
download_source "freetype-${FREETYPE_VERSION}.tar.xz" \
    "${FREETYPE_URL}" "${FREETYPE_SHA256}"
download_source "ffmpeg-${FFMPEG_VERSION}.tar.xz" \
    "${FFMPEG_URL}" "${FFMPEG_SHA256}"

for archive_path in \
    "${download_cache}/SDL3-${SDL3_VERSION}.tar.gz" \
    "${download_cache}/SDL3_image-${SDL3_IMAGE_VERSION}.tar.gz" \
    "${download_cache}/SDL3_ttf-${SDL3_TTF_VERSION}.tar.gz" \
    "${download_cache}/SDL3_mixer-${SDL3_MIXER_VERSION}.tar.gz" \
    "${download_cache}/freetype-${FREETYPE_VERSION}.tar.xz" \
    "${download_cache}/ffmpeg-${FFMPEG_VERSION}.tar.xz"; do
    tar -xf "${archive_path}" -C "${source_root}"
done

sdl_source="${source_root}/SDL3-${SDL3_VERSION}"
image_source="${source_root}/SDL3_image-${SDL3_IMAGE_VERSION}"
ttf_source="${source_root}/SDL3_ttf-${SDL3_TTF_VERSION}"
mixer_source="${source_root}/SDL3_mixer-${SDL3_MIXER_VERSION}"
ffmpeg_source="${source_root}/ffmpeg-${FFMPEG_VERSION}"
mv -- "${source_root}/freetype-${FREETYPE_VERSION}" \
    "${ttf_source}/external/freetype"

common_cmake_arguments=(
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_INSTALL_PREFIX=${sdl_prefix}"
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_BUILD_RPATH_USE_ORIGIN=ON
    "-DCMAKE_INSTALL_RPATH=\$ORIGIN"
    "-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG -ffile-prefix-map=${work_directory}=."
)

cmake -S "${sdl_source}" -B "${build_root}/SDL3" \
    "${common_cmake_arguments[@]}" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_INSTALL=ON \
    -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
cmake --build "${build_root}/SDL3" --parallel "${parallel_jobs}"
cmake --install "${build_root}/SDL3"

cmake -S "${image_source}" -B "${build_root}/SDL3_image" \
    "${common_cmake_arguments[@]}" \
    "-DCMAKE_PREFIX_PATH=${sdl_prefix}" \
    -DBUILD_SHARED_LIBS=ON -DSDLIMAGE_INSTALL=ON \
    -DSDLIMAGE_SAMPLES=OFF -DSDLIMAGE_TESTS=OFF \
    -DSDLIMAGE_BACKEND_STB=ON -DSDLIMAGE_AVIF=OFF \
    -DSDLIMAGE_JXL=OFF -DSDLIMAGE_TIF=OFF -DSDLIMAGE_WEBP=OFF \
    -DSDLIMAGE_DEPS_SHARED=OFF -DSDLIMAGE_STRICT=ON
cmake --build "${build_root}/SDL3_image" --parallel "${parallel_jobs}"
cmake --install "${build_root}/SDL3_image"

cmake -S "${ttf_source}" -B "${build_root}/SDL3_ttf" \
    "${common_cmake_arguments[@]}" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG -ffile-prefix-map=${work_directory}=." \
    "-DCMAKE_PREFIX_PATH=${sdl_prefix}" \
    -DBUILD_SHARED_LIBS=ON -DSDLTTF_INSTALL=ON -DSDLTTF_SAMPLES=OFF \
    -DSDLTTF_VENDORED=ON -DSDLTTF_STRICT=ON \
    -DSDLTTF_HARFBUZZ=OFF -DSDLTTF_PLUTOSVG=OFF
cmake --build "${build_root}/SDL3_ttf" --parallel "${parallel_jobs}"
cmake --install "${build_root}/SDL3_ttf"

cmake -S "${mixer_source}" -B "${build_root}/SDL3_mixer" \
    "${common_cmake_arguments[@]}" \
    "-DCMAKE_PREFIX_PATH=${sdl_prefix}" \
    -DBUILD_SHARED_LIBS=ON -DSDLMIXER_INSTALL=ON \
    -DSDLMIXER_TESTS=OFF -DSDLMIXER_EXAMPLES=OFF \
    -DSDLMIXER_DEPS_SHARED=OFF -DSDLMIXER_VENDORED=OFF \
    -DSDLMIXER_STRICT=ON \
    -DSDLMIXER_FLAC=ON -DSDLMIXER_FLAC_LIBFLAC=OFF \
    -DSDLMIXER_FLAC_DRFLAC=ON -DSDLMIXER_GME=OFF \
    -DSDLMIXER_MOD=OFF -DSDLMIXER_MP3=ON \
    -DSDLMIXER_MP3_DRMP3=ON -DSDLMIXER_MP3_MPG123=OFF \
    -DSDLMIXER_MIDI=ON -DSDLMIXER_MIDI_FLUIDSYNTH=OFF \
    -DSDLMIXER_MIDI_TIMIDITY=ON -DSDLMIXER_OPUS=OFF \
    -DSDLMIXER_VORBIS_STB=ON -DSDLMIXER_VORBIS_VORBISFILE=OFF \
    -DSDLMIXER_VORBIS_TREMOR=OFF -DSDLMIXER_WAVPACK=OFF
cmake --build "${build_root}/SDL3_mixer" --parallel "${parallel_jobs}"
cmake --install "${build_root}/SDL3_mixer"

mkdir -p "${build_root}/FFmpeg"
(
    cd "${build_root}/FFmpeg"
    ../../source/"ffmpeg-${FFMPEG_VERSION}"/configure \
        --prefix=/ \
        --libdir=/lib \
        --incdir=/include \
        --enable-shared \
        --disable-static \
        --enable-pic \
        --disable-x86asm \
        --disable-programs \
        --disable-doc \
        --disable-debug \
        --disable-autodetect \
        --disable-avdevice \
        --disable-avfilter \
        --disable-postproc
    make -j"${parallel_jobs}"
    make DESTDIR="${ffmpeg_install}" install
)

cp -a "${sdl_prefix}/include/." "${stage_root}/include/"
cp -a "${ffmpeg_install}/include/." "${stage_root}/include/"

shopt -s nullglob
runtime_libraries=(
    "${sdl_prefix}"/lib/libSDL3*.so*
    "${ffmpeg_install}"/lib/libavcodec.so*
    "${ffmpeg_install}"/lib/libavformat.so*
    "${ffmpeg_install}"/lib/libavutil.so*
    "${ffmpeg_install}"/lib/libswresample.so*
    "${ffmpeg_install}"/lib/libswscale.so*
)
if (( ${#runtime_libraries[@]} == 0 )); then
    echo "Dependency build produced no shared libraries." >&2
    exit 1
fi
cp -a "${runtime_libraries[@]}" "${stage_root}/lib/"
shopt -u nullglob

# FFmpeg's makefiles consume dollar signs in --extra-ldflags at multiple
# expansion layers. Set the final relative path on every staged ELF directly;
# this also normalizes SDL and FFmpeg to the same private-library policy.
while IFS= read -r -d '' library_path; do
    patchelf --set-rpath '$ORIGIN' "${library_path}"
done < <(find "${stage_root}/lib" -type f -name '*.so.*' -print0)

cp "${script_dir}/dependencies-licenses.md" \
    "${stage_root}/licenses/README.md"
cp "${sdl_source}/LICENSE.txt" "${stage_root}/licenses/SDL3.txt"
cp "${sdl_source}/src/hidapi/LICENSE.txt" \
    "${stage_root}/licenses/SDL3-hidapi.txt"
cp "${sdl_source}/src/hidapi/LICENSE-bsd.txt" \
    "${stage_root}/licenses/SDL3-hidapi-bsd.txt"
cp "${sdl_source}/src/hidapi/LICENSE-orig.txt" \
    "${stage_root}/licenses/SDL3-hidapi-original.txt"
cp "${sdl_source}/src/video/yuv2rgb/LICENSE" \
    "${stage_root}/licenses/SDL3-yuv2rgb.txt"
cp "${image_source}/LICENSE.txt" "${stage_root}/licenses/SDL3_image.txt"
sed -n '/This software is available under 2 licenses/,$p' \
    "${image_source}/src/stb_image.h" > \
    "${stage_root}/licenses/SDL3_image-stb.txt"
cp "${ttf_source}/LICENSE.txt" "${stage_root}/licenses/SDL3_ttf.txt"
cp "${ttf_source}/external/freetype/LICENSE.TXT" \
    "${stage_root}/licenses/FreeType.txt"
cp "${ttf_source}/external/freetype/docs/FTL.TXT" \
    "${stage_root}/licenses/FreeType-FTL.txt"
cp "${mixer_source}/LICENSE.txt" "${stage_root}/licenses/SDL3_mixer.txt"
cp "${mixer_source}/src/dr_libs/LICENSE" \
    "${stage_root}/licenses/SDL3_mixer-dr_libs.txt"
sed -n '/This software is available under 2 licenses/,$p' \
    "${mixer_source}/src/stb_vorbis/stb_vorbis.h" > \
    "${stage_root}/licenses/SDL3_mixer-stb_vorbis.txt"
cp "${mixer_source}/src/timidity/COPYING" \
    "${stage_root}/licenses/SDL3_mixer-timidity.txt"
cp "${ffmpeg_source}/LICENSE.md" \
    "${stage_root}/licenses/FFmpeg-LICENSE.md"
cp "${ffmpeg_source}/COPYING.LGPLv2.1" \
    "${stage_root}/licenses/FFmpeg-LGPL-2.1.txt"

while IFS= read -r -d '' library_path; do
    strip --strip-unneeded "${library_path}"
done < <(find "${stage_root}/lib" -type f -name '*.so.*' -print0)

printf '%s\n' "${dependency_set_id}" > "${stage_root}/.dependency-set"

# Keep the committed and packaged dependency tree reproducible even when the
# build container and host shared filesystem apply different default modes.
find "${stage_root}" -type d -exec chmod 0755 {} +
find "${stage_root}" -type f -exec chmod 0644 {} +

for required_file in "${required_files[@]}"; do
    if [[ ! -e "${stage_root}/${required_file}" ]]; then
        echo "Dependency staging file is missing: ${required_file}" >&2
        exit 1
    fi
done

while IFS= read -r -d '' library_path; do
    if strings "${library_path}" | grep -F -e "${work_directory}" \
        -e "${repository_root}" >/dev/null; then
        echo "Build-machine path leaked into ${library_path}" >&2
        exit 1
    fi
    if ! readelf -d "${library_path}" | grep -Eq \
        '(RPATH|RUNPATH).*[[]\$ORIGIN[]]'; then
        echo "Private runtime path is missing from ${library_path}" >&2
        exit 1
    fi
done < <(find "${stage_root}/lib" -type f -name '*.so.*' -print0)

if LD_LIBRARY_PATH="${stage_root}/lib" \
    ldd "${stage_root}/lib/libSDL3_ttf.so.0" | grep -q 'not found'; then
    echo "A staged SDL dependency could not be resolved." >&2
    exit 1
fi
if LD_LIBRARY_PATH="${stage_root}/lib" \
    ldd "${stage_root}/lib/libavformat.so.59" | grep -q 'not found'; then
    echo "A staged FFmpeg dependency could not be resolved." >&2
    exit 1
fi

deployment_root="${dependency_root}.new.$$"
previous_root="${dependency_root}.previous.$$"
rm -rf -- "${deployment_root}" "${previous_root}"
mkdir -p "$(dirname "${dependency_root}")"
if ! cp -a "${stage_root}" "${deployment_root}"; then
    # Some shared-folder filesystems (notably VMware hgfs on a Windows host)
    # reject symlink creation. Keep the same SONAME filenames as regular
    # copies there; normal Linux filesystems retain the relative symlink chain.
    echo "The target filesystem does not support symlinks; materializing SONAME links." >&2
    rm -rf -- "${deployment_root}"
    mkdir -p "${deployment_root}"
    cp -aL "${stage_root}/." "${deployment_root}/"
fi
if [[ -e "${dependency_root}" ]]; then
    mv -- "${dependency_root}" "${previous_root}"
fi
if ! mv -- "${deployment_root}" "${dependency_root}"; then
    if [[ -e "${previous_root}" ]]; then
        mv -- "${previous_root}" "${dependency_root}"
    fi
    exit 1
fi

if ! dependencies_are_ready; then
    rm -rf -- "${dependency_root}"
    if [[ -e "${previous_root}" ]]; then
        mv -- "${previous_root}" "${dependency_root}"
    fi
    echo "Installed dependency set failed its completeness check." >&2
    exit 1
fi
rm -rf -- "${previous_root}"

echo "Private Linux dependencies installed in ${dependency_root}"
