#!/usr/bin/env bash
set -e

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -d "${script_dir}/bin/linux" ]]; then
    runtime_dir="${script_dir}/bin/linux"
else
    runtime_dir="${script_dir}"
fi
cd "${runtime_dir}"

executable="jxqy-all-in-one"
if [[ -f "${executable}" ]]; then
    chmod u+x "${executable}"
    export LD_LIBRARY_PATH="${PWD}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
    exec "./${executable}" "$@"
fi

echo "No Linux game executable was found in ${PWD}. Build the project first." >&2
exit 1
