#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
freecad_bin="${FREECAD_EXECUTABLE:-}"

if [[ -z "$freecad_bin" ]]; then
  if command -v FreeCAD >/dev/null 2>&1; then
    freecad_bin="$(command -v FreeCAD)"
  elif command -v freecad >/dev/null 2>&1; then
    freecad_bin="$(command -v freecad)"
  else
    echo "FreeCAD was not found. Set FREECAD_EXECUTABLE to its GUI executable." >&2
    exit 127
  fi
fi

exec "$freecad_bin" \
  -M "$repo_root/freecad/DatumLoopDS" \
  -P "$repo_root/src" \
  "$repo_root/freecad/DatumLoopDS/Launch.FCMacro" \
  "$@"
