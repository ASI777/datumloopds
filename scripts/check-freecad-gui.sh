#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
freecad_bin="${FREECAD_EXECUTABLE:-}"
extra_python_args=()

if [[ -n "${DATUMLOOPDS_FREECAD_EXTRA_PYTHON_PATHS:-}" ]]; then
  IFS=: read -r -a extra_python_paths <<< "${DATUMLOOPDS_FREECAD_EXTRA_PYTHON_PATHS:-}"
  for python_path in "${extra_python_paths[@]}"; do
    if [[ -n "$python_path" ]]; then
      extra_python_args+=(-P "$python_path")
    fi
  done
fi

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

log_file="$(mktemp)"
trap 'rm -f "$log_file"' EXIT
timeout "${DATUMLOOPDS_GUI_SMOKE_TIMEOUT_SECONDS:-30}s" "$freecad_bin" \
  -M "$repo_root/freecad/DatumLoopDS" \
  -P "$repo_root/src" \
  "${extra_python_args[@]}" \
  "$repo_root/tests/FreeCADGuiSmoke.FCMacro" >"$log_file" 2>&1 || command_status=$?
cat "$log_file"
if [[ "${command_status:-0}" -ne 0 ]]; then
  echo "FreeCAD GUI smoke exited with status ${command_status}." >&2
  exit "$command_status"
fi
if ! grep -Fq "DATUMLOOP_GUI_SMOKE_PASS" "$log_file"; then
  echo "FreeCAD GUI smoke did not report success." >&2
  exit 1
fi
