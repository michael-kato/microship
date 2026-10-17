#!/usr/bin/env bash
set -euo pipefail

started_seconds=$SECONDS
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/.." && pwd)"
build_dir="$repo_root/build/windows-cross"
gpu_tests=OFF
if [[ "${1:-}" == --gpu && $# == 1 ]]; then
    gpu_tests=ON
    export NANOVA_VULKAN_VALIDATION=1
    python3 "$script_dir/gpu_environment.py"
elif [[ "${1:-}" == --cpu && $# == 1 ]]; then
    gpu_tests=OFF
elif [[ $# != 0 ]]; then
    printf 'Usage: %s [--cpu|--gpu]\n' "$0" >&2
    exit 2
fi

cmake_args=(-S "$repo_root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    -DCMAKE_TOOLCHAIN_FILE="$repo_root/cmake/windows-mingw.cmake"
    "-DCMAKE_CROSSCOMPILING_EMULATOR=python3;$script_dir/proton_run.py"
    -DNANOVA_GPU_TESTS="$gpu_tests")
if [[ ! -f "$build_dir/CMakeCache.txt" ]] && command -v ninja >/dev/null 2>&1; then
    cmake_args+=(-G Ninja)
fi

cmake "${cmake_args[@]}"
cmake --build "$build_dir" --config Release --parallel
(
    cd -- "$build_dir"
    ctest --build-config Release --output-on-failure
)

printf 'Build and E2E completed in %ss.\n' "$((SECONDS - started_seconds))"
