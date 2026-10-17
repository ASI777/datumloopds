#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
executable="${DATUMLOOPDS_EXECUTABLE:-${repo_root}/build/native/apps/datumloop-desktop/DatumLoopDS}"

if [[ ! -x "${executable}" ]]; then
    echo "Native DatumLoop executable not found or not executable: ${executable}" >&2
    exit 1
fi

if ! command -v ldd >/dev/null 2>&1; then
    echo "ldd is required to inspect the native executable's dynamic dependencies." >&2
    exit 1
fi

dependencies="$(ldd "${executable}")"
if rg -i 'FreeCAD(App|Gui)?|PartGui|libPart\.so|libCoin|Coin3D|SoQt' <<<"${dependencies}"; then
    echo "Native executable links to a FreeCAD, Coin, or SoQt runtime dependency." >&2
    exit 1
fi

source_roots=(
    "${repo_root}/apps/datumloop-desktop"
    "${repo_root}/src/datumloop/core"
    "${repo_root}/src/datumloop/geometry"
    "${repo_root}/src/datumloop/renderer"
    "${repo_root}/src/datumloop/ui"
)
if rg -n -i '#\s*include\s*[<"][^">]*(FreeCAD|Coin3D|SoQt|Gui/Document|Gui/View3DInventor)' "${source_roots[@]}"; then
    echo "Native source includes a FreeCAD GUI, Coin3D, or SoQt header." >&2
    exit 1
fi

echo "Native independence check passed: no FreeCAD, Coin, or SoQt dynamic dependencies or native include bindings."
