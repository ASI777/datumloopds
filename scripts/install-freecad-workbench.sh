#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mod_root="${DATUMLOOPDS_FREECAD_MOD_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/DatumLoopDS/v1-1/Mod}"
install_root="$mod_root/DatumLoopDS"

mkdir -p "$install_root"
cp -R "$repo_root/freecad/DatumLoopDS/." "$install_root/"
mkdir -p "$install_root/datumloopds"
cp -R "$repo_root/src/datumloopds/." "$install_root/datumloopds/"

printf 'Installed DatumLoop DS workbench to the branded application profile: %s\n' "$install_root"
printf 'Restart FreeCAD, then select the DatumLoop DS workbench.\n'
