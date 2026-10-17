# DatumLoop DS

DatumLoop DS is an open-source engineering application for designing and
evaluating industrial mechanisms. Its primary architecture is a standalone
C++20 / Qt 6 / QML application with DatumLoop-owned documents and commands and
OCCT as its exact-geometry kernel. The FreeCAD Python workbench and geometry
adapters remain available as a compatibility and migration path for existing
`.FCStd` projects.

## Status

The repository contains the native C++/Qt application and the retained FreeCAD
compatibility workbench and Python geometry adapters. The public [DatumLoop DS repository](https://github.com/ASI777/datumloopds)
contains both paths. The
public [FreeCAD fork](https://github.com/ASI777/FreeCAD) and
[smlibpy fork](https://github.com/ASI777/smlibpy) are available. The FreeCAD
fork's `codex/datumloop-ds-1.1.3` branch contains the branding and isolated
application identity changes. The standalone application migration is tracked in
[`docs/native-migration-dependencies.md`](docs/native-migration-dependencies.md).

## Development

The compatibility workbench targets Linux and Python 3.11 or newer. It uses the
FreeCAD/PySide runtime supplied by FreeCAD. The native application targets C++20,
Qt 6 Quick/QML, and OCCT; it does not require FreeCAD.

Run the test suite and build the Python package with:

```sh
make check
make freecad-check
make build
```

Build and run the native application and its C++ core and UI-model checks with:

```sh
make native-check
make native-independence-check
./build/native/apps/datumloop-desktop/DatumLoopDS
```

The native shell can open and save version-three `.dld` projects. The provisional
format stores project/object/feature UUIDs, hierarchy, typed scalar properties,
translations in millimetres, exact OCCT B-Rep shapes, initial persistent
face/edge/vertex references, and a recomputable primitive/transform/Boolean
feature graph. Object-to-object references, rotations, and simulation state
remain unsupported; see
[`docs/native-project-format.md`](docs/native-project-format.md).

`make native-smoke` starts the native window and verifies that Qt Quick
presented a frame; run it from a desktop session with a working graphics
display. `make native-qml-smoke` verifies QML and OCCT-derived viewport data
initialization in headless environments; it does not verify 3D rendering. The
`make native-pointer-smoke` uses XTest under Xvfb to exercise object and face
picking, camera navigation, and the Move cancel/confirm/undo/redo workflow. The
app can also be built and run without FreeCAD.

## Exact geometry backends

OCCT is the target production geometry kernel for native documents, assemblies,
STEP/IGES workflows, and manufacturing geometry. The existing
`datumloopds.geometry` boundary keeps FreeCAD `Part` and SMLib native shapes
behind adapters and reports unsupported operations with
`UnsupportedKernelOperation`; it never falls back to tessellated meshes for
exact modeling. The native C++ geometry API is being added beside this Python
compatibility layer.

SMLib is an optional second B-Rep/NURBS backend for geometry experiments and
USD B-Rep workflows. The adapter targets smlibpy 0.3.4, which exposes the
required primitives, Booleans, features, measures, tessellation, STEP, native
`.smb`, and text USD `BrepArray` APIs. Install it only where that backend is
needed:

```sh
python -m pip install '.[smlib]'
```

Convert exact shapes between adapters through STEP with
`datumloopds.geometry.convert_shape`; the conversion can change tolerances or
topology and should be revalidated. On the compatibility path, FreeCAD owns
document mutation in its GUI thread. The adapters themselves operate on shapes
and do not mutate live documents.

Run the seven fixed comparison cases and write JSON plus Markdown reports with:

```sh
make geometry-benchmark
```

The benchmark runs FreeCADCmd and the optional smlibpy runtime separately. It
reports each kernel's full capability map and compares validity, volume, area,
bounds, center of mass, topology counts, operation and tessellation time,
triangle count, and STEP round-trip. You can add named existing STEP inputs by
setting `DATUMLOOPDS_STEP_FILES_JSON` to a JSON array of `name` and `path`
objects; source files stay in place, and only their basenames and measurements
plus byte sizes are written. FreeCAD's tessellator accepts a chord tolerance
but no angular tolerance; the report records that limitation.

`make freecad-check` uses `FreeCADCmd` to exercise the workbench widgets and
project save/reopen path. To check the real GUI viewport on a machine with a
working desktop display, run `./scripts/check-freecad-gui.sh`.

To target a specific fork build, set `FREECADCMD_EXECUTABLE` for the command
line check and `FREECAD_EXECUTABLE` for the GUI check. If its embedded Python
needs additional modules, set `DATUMLOOPDS_FREECAD_EXTRA_PYTHON_PATHS` to a
colon-separated list of directories; the check scripts pass each directory to
FreeCAD with `-P`.

Launch the source workbench in FreeCAD with:

```sh
./scripts/launch-freecad.sh
```

Set `FREECAD_EXECUTABLE` if the FreeCAD binary is not named `FreeCAD` or
`freecad` on `PATH`. To install the workbench into the branded DatumLoop DS
application profile, run:

```sh
./scripts/install-freecad-workbench.sh
```

The installed workbench is named **DatumLoop DS**. Its central 3D view remains
FreeCAD's CAD viewport. Project creation, opening, and saving operate on
standard `.FCStd` documents so project data stays inspectable in FreeCAD.
The installer uses the `DatumLoopDS/v1-1/Mod` profile path, matching the
FreeCAD 1.1 fork's separate settings directory. Set
`DATUMLOOPDS_FREECAD_MOD_DIR` when developing against an unbranded FreeCAD
installation.

## Repository layout

- `apps/datumloop-desktop/` — standalone Qt 6 application entry point.
- `src/datumloop/` — native DatumLoop core, document I/O, workspace, OCCT, and UI models.
- `src/datumloopds/` — Python project metadata and retained FreeCAD compatibility code.
- `src/datumloopds/geometry/` — backend-neutral geometry values, capabilities,
  OCCT and optional SMLib adapters, exact STEP conversion, and benchmark fixtures.
- `freecad/DatumLoopDS/` — FreeCAD workbench entry points and resources.
- `scripts/` — source launcher and per-user workbench installer.
- `docs/` — architecture and milestone decisions.
- `tests/` — repeatable checks; the FreeCADCmd smoke also exercises real OCCT
  modeling operations without changing a project document.
- `qml/` — native application workspaces, common editor hosts, viewport/outliner/properties
  editors, reusable controls, and design tokens.

## License

This repository currently declares GPL-3.0-or-later in `LICENSE` and package
metadata. The project's intended license and contributor ownership need a
product-owner decision before any license change. No files were relicensed for
the native migration. See [`docs/licensing-strategy.md`](docs/licensing-strategy.md)
and preserve upstream licenses and notices for FreeCAD and optional dependencies.
