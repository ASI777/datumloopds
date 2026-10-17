#!/usr/bin/env bash
set -euo pipefail

repo_root="${DATUMLOOP_SOURCE_DIR:-/workspace}"
build_dir="${DATUMLOOP_BUILD_DIR:-/tmp/datumloopds-native-build}"

cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DDATUMLOOP_ENABLE_MFEM=OFF
cmake --build "$build_dir" --parallel 2
ctest --test-dir "$build_dir" --output-on-failure
QT_QPA_PLATFORM=offscreen "$build_dir/apps/datumloop-desktop/DatumLoopDS" --smoke-load
