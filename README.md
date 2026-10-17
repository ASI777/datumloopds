# DatumLoop DS

DatumLoop DS is an open-source desktop platform for developing and evaluating
industrial mechanisms. It uses FreeCAD for CAD documents, assembly editing, and
the 3D viewport. The product workbench and engineering services live in this
repository; branding and any FreeCAD core changes belong in the dedicated
FreeCAD fork.

## Status

Milestone 1 is in progress. This repository contains the initial workbench,
project shell, package metadata, and reproducible local checks. The required
GitHub repositories and the fully branded FreeCAD build are not yet available.
The project baseline is FreeCAD 1.1.3. The installed development copy may be
older; only 1.1.3 will count toward the release gate.

## Development

The workbench targets Linux and Python 3.11 or newer. It uses only the Python
standard library and the FreeCAD/PySide runtime supplied by FreeCAD.

Run the test suite and build the Python package with:

```sh
make check
make freecad-check
make build
```

`make freecad-check` uses `FreeCADCmd` to exercise the workbench widgets and
project save/reopen path. To check the real GUI viewport on a machine with a
working desktop display, run `./scripts/check-freecad-gui.sh`.

Launch the source workbench in FreeCAD with:

```sh
./scripts/launch-freecad.sh
```

Set `FREECAD_EXECUTABLE` if the FreeCAD binary is not named `FreeCAD` or
`freecad` on `PATH`. To install the workbench into the current user's FreeCAD
module directory, run:

```sh
./scripts/install-freecad-workbench.sh
```

The installed workbench is named **DatumLoop DS**. Its central 3D view remains
FreeCAD's CAD viewport. Project creation, opening, and saving operate on
standard `.FCStd` documents so project data stays inspectable in FreeCAD.

## Repository layout

- `src/datumloopds/` — engine-independent project metadata and FreeCAD GUI code.
- `freecad/DatumLoopDS/` — FreeCAD workbench entry points and resources.
- `scripts/` — source launcher and per-user workbench installer.
- `docs/` — architecture and milestone decisions.
- `tests/` — repeatable checks that do not require a GUI session.

## License

New application modules are licensed under GPL-3.0-or-later. FreeCAD and future
solver forks retain their upstream licenses and notices. See `LICENSE` and the
license notices in each dependency repository.
