#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
python_bin="${PYTHON:-python3}"
freecad_cmd="${FREECADCMD_EXECUTABLE:-}"
if [[ -z "$freecad_cmd" ]]; then
  if command -v FreeCADCmd >/dev/null 2>&1; then
    freecad_cmd="$(command -v FreeCADCmd)"
  elif command -v freecadcmd >/dev/null 2>&1; then
    freecad_cmd="$(command -v freecadcmd)"
  else
    echo "FreeCADCmd was not found. Set FREECADCMD_EXECUTABLE to the console executable." >&2
    exit 127
  fi
fi

report_dir="${DATUMLOOPDS_BENCHMARK_DIR:-$repo_root/reports/geometry-benchmark}"
mkdir -p "$report_dir"
occt_report="$report_dir/occt.json"
smlib_report="$report_dir/smlib.json"
comparison_json="$report_dir/comparison.json"
comparison_markdown="$report_dir/comparison.md"
step_files_json="${DATUMLOOPDS_STEP_FILES_JSON:-[]}"
freecad_log="$(mktemp)"
trap 'rm -f "$freecad_log"' EXIT

if ! DATUMLOOPDS_BENCHMARK_OUTPUT="$occt_report" \
  DATUMLOOPDS_STEP_FILES_JSON="$step_files_json" \
  QT_QPA_PLATFORM=offscreen \
  XDG_CONFIG_HOME=/tmp/datumloopds-benchmark-config \
  XDG_DATA_HOME=/tmp/datumloopds-benchmark-data \
  XDG_CACHE_HOME=/tmp/datumloopds-benchmark-cache \
  "$freecad_cmd" \
    -M "$repo_root/freecad/DatumLoopDS" \
    -P "$repo_root/src" \
    "$repo_root/scripts/FreeCADGeometryBenchmark.FCMacro" >"$freecad_log" 2>&1; then
  cat "$freecad_log"
  echo "FreeCADCmd geometry benchmark failed." >&2
  exit 1
fi
cat "$freecad_log"
if ! rg -q "DATUMLOOPDS_BENCHMARK_PASS" "$freecad_log"; then
  echo "FreeCADCmd did not report benchmark success." >&2
  exit 1
fi

DATUMLOOPDS_STEP_FILES_JSON="$step_files_json" \
PYTHONPATH="$repo_root/src${PYTHONPATH:+:$PYTHONPATH}" \
  "$python_bin" "$repo_root/scripts/benchmark_geometry.py" --backend smlib --output "$smlib_report"
"$python_bin" "$repo_root/scripts/compare_geometry_benchmarks.py" \
  --occt "$occt_report" \
  --smlib "$smlib_report" \
  --json "$comparison_json" \
  --markdown "$comparison_markdown"
