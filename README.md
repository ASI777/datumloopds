# DatumLoop DS

DatumLoop DS is an open-source engineering application for designing and
evaluating industrial mechanisms. The current Python workbench and geometry
adapters run inside FreeCAD. The product is moving toward a standalone Qt 6 / QML
application with DatumLoop-owned documents and commands and OCCT as its primary
exact-geometry kernel. The FreeCAD workbench remains available as a compatibility
and migration path until native workflows replace it.

## Status

The repository contains the FreeCAD compatibility workbench, geometry adapters,
and the new native C++/Qt application slice. The public [DatumLoop DS repository](https://github.com/ASI777/datumloopds)
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
./build/native/apps/datumloop-desktop/DatumLoopDS
```

`make native-smoke` starts the native window and verifies that Qt Quick
presented a frame; run it from a desktop session with a working graphics
display. `make native-qml-smoke` verifies QML and OCCT-derived viewport data
initialization in headless environments; it does not verify 3D rendering. The
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
topology and should be revalidated. FreeCAD owns document mutation in the GUI
thread as before. The adapters themselves operate on shapes and do not mutate
live documents.

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
- `src/datumloop/` — native DatumLoop core, document, workspace, OCCT, and UI models.
- `src/datumloopds/` — Python project metadata and retained FreeCAD compatibility code.
- `src/datumloopds/geometry/` — backend-neutral geometry values, capabilities,
  OCCT and optional SMLib adapters, exact STEP conversion, and benchmark fixtures.
- `freecad/DatumLoopDS/` — FreeCAD workbench entry points and resources.
- `scripts/` — source launcher and per-user workbench installer.
- `docs/` — architecture and milestone decisions.
- `tests/` — repeatable checks; the FreeCADCmd smoke also exercises real OCCT
  modeling operations without changing a project document.
- `qml/` — native application workspaces, editor components, and design tokens.

## License

New application modules are licensed under GPL-3.0-or-later. FreeCAD and future
solver forks retain their upstream licenses and notices. See `LICENSE` and the
license notices in each dependency repository.
