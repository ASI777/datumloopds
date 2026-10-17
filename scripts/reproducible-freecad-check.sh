#!/usr/bin/env bash
set -euo pipefail

repo_root="${DATUMLOOP_SOURCE_DIR:-/workspace}"
source_dir="${FREECAD_SOURCE_DIR:-/freecad-source}"
expected_revision="${FREECAD_SOURCE_REVISION:-b6b11c9cabeeee14fd192856f5749af620ea7183}"
actual_revision="$(git -C "$source_dir" rev-parse HEAD)"
build_dir="${FREECAD_BUILD_DIR:-/tmp/datumloopds-freecad-build}"
install_dir="${FREECAD_INSTALL_DIR:-/tmp/datumloopds-freecad-install}"
smoke_root="${FREECAD_SMOKE_ROOT:-/tmp/datumloopds-freecad-repro-smoke}"

if [[ "$actual_revision" != "$expected_revision" ]]; then
    echo "FreeCAD source revision mismatch: expected $expected_revision, found $actual_revision" >&2
    exit 1
fi

if git -C "$source_dir" submodule status --recursive | grep -Eq '^[-+U]'; then
    echo "FreeCAD source has an uninitialized, changed, or conflicted submodule." >&2
    git -C "$source_dir" submodule status --recursive >&2
    exit 1
fi

mkdir -p "$smoke_root/home" "$smoke_root/config" "$smoke_root/data" "$smoke_root/cache"
export HOME="$smoke_root/home"
export XDG_CONFIG_HOME="$smoke_root/config"
export XDG_DATA_HOME="$smoke_root/data"
export XDG_CACHE_HOME="$smoke_root/cache"
export PYTHONNOUSERSITE=1
unset PYTHONPATH PYTHONUSERBASE LD_LIBRARY_PATH QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH

cmake -S "$source_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_GUI=ON \
    -DBUILD_TEST=OFF \
    -DBUILD_TESTING=OFF \
    -DENABLE_DEVELOPER_TESTS=ON \
    -DINSTALL_TO_SITEPACKAGES=OFF \
    -DCMAKE_INSTALL_PREFIX="$install_dir"
cmake --build "$build_dir" --parallel "${FREECAD_BUILD_JOBS:-2}"
cmake --install "$build_dir"

freecad_cmd="$install_dir/bin/FreeCADCmd"
freecad_gui="$install_dir/bin/FreeCAD"
test -x "$freecad_cmd"
test -x "$freecad_gui"

FREECADCMD_EXECUTABLE="$freecad_cmd" "$repo_root/scripts/check-freecad-core.sh"

export LIBGL_ALWAYS_SOFTWARE=1
export QT_OPENGL=software
export QT_QPA_PLATFORM=xcb
export FREECAD_EXECUTABLE="$freecad_gui"
export DATUMLOOPDS_GUI_SMOKE_SCREENSHOT="$smoke_root/freecad-gui.png"
unset WAYLAND_DISPLAY
xvfb-run --auto-servernum "$repo_root/scripts/check-freecad-gui.sh"
test -s "$DATUMLOOPDS_GUI_SMOKE_SCREENSHOT"

echo "DATUMLOOPDS_FREECAD_REPRODUCIBLE_BUILD_PASS: $actual_revision"
