#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
freecad_cmd="${FREECADCMD_EXECUTABLE:-}"
extra_python_args=()

if [[ -n "${DATUMLOOPDS_FREECAD_EXTRA_PYTHON_PATHS:-}" ]]; then
  IFS=: read -r -a extra_python_paths <<< "${DATUMLOOPDS_FREECAD_EXTRA_PYTHON_PATHS:-}"
  for python_path in "${extra_python_paths[@]}"; do
    if [[ -n "$python_path" ]]; then
      extra_python_args+=(-P "$python_path")
    fi
  done
fi

if [[ -z "$freecad_cmd" ]]; then
  if command -v FreeCADCmd >/dev/null 2>&1; then
    freecad_cmd="$(command -v FreeCADCmd)"
  elif command -v freecadcmd >/dev/null 2>&1; then
    freecad_cmd="$(command -v freecadcmd)"
  else
    echo "FreeCADCmd was not found. Set FREECADCMD_EXECUTABLE to its console executable." >&2
    exit 127
  fi
fi

output="$(QT_QPA_PLATFORM=offscreen \
  XDG_CONFIG_HOME=/tmp/datumloopds-config \
  XDG_DATA_HOME=/tmp/datumloopds-data \
  XDG_CACHE_HOME=/tmp/datumloopds-cache \
  "$freecad_cmd" \
  -M "$repo_root/freecad/DatumLoopDS" \
  -P "$repo_root/src" \
  "${extra_python_args[@]}" \
  "$repo_root/tests/FreeCADWidgetSmoke.FCMacro" 2>&1)"
printf '%s\n' "$output"
if ! rg -q "DATUMLOOP_FREECAD_CORE_SMOKE_PASS" <<<"$output"; then
  echo "FreeCAD core smoke did not report success." >&2
  exit 1
fi
