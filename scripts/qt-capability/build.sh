#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="$repo_root/build/qt-capability"

cmake -S "$repo_root/desktop/qt-capability" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build_dir" -j "${QT_CAPABILITY_JOBS:-4}"
ctest --test-dir "$build_dir" --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
    -s "$repo_root/scripts/qt-capability/tests" -v
