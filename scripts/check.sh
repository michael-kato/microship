#!/usr/bin/env bash
set -euo pipefail

started_seconds=$SECONDS
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/.." && pwd)"
build_dir="$repo_root/build/windows-cross"
gpu_tests=OFF
check_mode=quick
quick_scope=all
if [[ "${1:-}" == --gpu && $# == 1 ]]; then
    check_mode=full
    gpu_tests=ON
    export NANOVA_VULKAN_VALIDATION=1
    python3 "$script_dir/gpu_environment.py"
elif [[ "${1:-}" == --cpu && $# == 1 ]]; then
    check_mode=full
elif [[ "${1:-}" == --quick && $# -le 2 ]]; then
    quick_scope="${2:-all}"
elif [[ $# != 0 ]]; then
    printf 'Usage: %s [--quick [all|fungal|repair|smoke]|--cpu|--gpu]\n' "$0" >&2
    exit 2
fi
case "$quick_scope" in
    all|fungal|repair|smoke) ;;
    *) printf 'Unknown quick scope: %s\n' "$quick_scope" >&2; exit 2 ;;
esac

python3 "$script_dir/format_json.py" --check

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
if [[ "$check_mode" == quick ]]; then
    printf 'Quick CPU checks (%s). Use --cpu for the complete suite.\n' "$quick_scope"
    ctest --test-dir "$build_dir" --build-config Release --output-on-failure -R '^smoke$'
    if [[ "$quick_scope" == all || "$quick_scope" == repair ]]; then
        python3 "$repo_root/tests/e2e/factory_repair.py" "$build_dir/nanova.exe" --quick
    fi
    if [[ "$quick_scope" == all || "$quick_scope" == fungal ]]; then
        python3 "$repo_root/tests/e2e/fungal_defence.py" "$build_dir/nanova.exe" --quick
    fi
else
    ctest --test-dir "$build_dir" --build-config Release --output-on-failure
fi

printf 'Incremental build and %s checks completed in %ss.\n' "$check_mode" "$((SECONDS - started_seconds))"
