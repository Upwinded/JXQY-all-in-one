#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd -- "${script_dir}/.." && pwd)"

build_type="Release"
build_testing="OFF"
build_automation_tools="OFF"
enable_automation_hooks="OFF"
build_directory="${repository_root}/build/linux-release"
build_tests=false

build_jobs="${JXQY_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)}"
if [[ ! "${build_jobs}" =~ ^[1-9][0-9]*$ ]]; then
    echo "JXQY_BUILD_JOBS must be a positive integer." >&2
    exit 2
fi
if [[ -z "${JXQY_BUILD_JOBS:-}" ]] && (( build_jobs > 8 )); then
    build_jobs=8
fi

if (( $# > 1 )); then
    echo "Usage: $0 [--tests]" >&2
    exit 2
fi

if (( $# == 1 )); then
    case "$1" in
        --tests)
            build_type="Debug"
            build_testing="ON"
            build_automation_tools="ON"
            enable_automation_hooks="ON"
            build_directory="${repository_root}/build/linux-tests"
            build_tests=true
            ;;
        *)
            echo "Unknown argument: $1" >&2
            echo "Usage: $0 [--tests]" >&2
            exit 2
            ;;
    esac
fi

if ! bash "${script_dir}/build-dependencies.sh" --check; then
    echo "Private Linux dependencies are missing or outdated; building them now."
    bash "${script_dir}/build-dependencies.sh"
fi

cmake \
    -S "${repository_root}" \
    -B "${build_directory}" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DBUILD_TESTING="${build_testing}" \
    -DJXQY_BUILD_AUTOMATION_TOOLS="${build_automation_tools}" \
    -DJXQY_ENABLE_AUTOMATION_HOOKS="${enable_automation_hooks}" \
    -DJXQY_BUILD_BENCHMARKS=OFF

if [[ "${build_tests}" == true ]]; then
    cmake --build "${build_directory}" --parallel "${build_jobs}"
    (
        cd "${build_directory}"
        ctest --output-on-failure \
            --parallel "${build_jobs}"
    )
else
    cmake --build "${build_directory}" \
        --target jxqy-all-in-one jxqy-program-updater \
        --parallel "${build_jobs}"
    strip --strip-unneeded \
        "${repository_root}/bin/linux/jxqy-all-in-one" \
        "${repository_root}/bin/updater/linux/jxqy-program-updater"
fi
