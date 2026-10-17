# DatumLoop DS Milestone Status

**Milestone 1 remains in progress because a clean FreeCAD fork configure and build has not yet been repeated from an empty build tree.** The configured FreeCAD 1.1.3 build completed, and its command-line and GUI smoke checks passed. The product's long-term direction is now the standalone C++20 / Qt 6 / QML application; FreeCAD remains a migration and compatibility path until native workflows replace it. See [Native Application Migration](native-migration-dependencies.md).

## Milestone 1 delivery

- The public [ASI777/datumloopds](https://github.com/ASI777/datumloopds) repository contains the compatibility workbench, project metadata, geometry adapters, engineering services, and the native Qt application slice.
- The public [ASI777/FreeCAD](https://github.com/ASI777/FreeCAD) fork has a FreeCAD 1.1.3-based `codex/datumloop-ds-1.1.3` branch with application branding, DatumLoop artwork, a desktop entry, and the DatumLoop start workbench. Its DatumLoopDS executable identity gives it a separate settings profile while retaining FreeCAD's CAD engine.
- The public [ASI777/smlibpy](https://github.com/ASI777/smlibpy) fork is available and currently exposes the upstream-compatible smlibpy 0.3.4 API.
- The compatibility workbench installer targets `$XDG_DATA_HOME/DatumLoopDS/v1-1/Mod` by default.

## Geometry and migration status

- The Python geometry contract defines portable shape/value types, capabilities, typed unsupported-operation errors, and backend adapters. Its FreeCAD Part adapter remains the legacy CAD path. The native C++ application uses direct OCCT APIs as its long-term primary geometry path; SMLib remains optional.
- Exact cross-kernel conversion goes through STEP. Tessellation remains a display and analysis representation, not an exact modeling source.
- The deterministic seven-case OCCT / SMLib benchmark matched volume, area, bounds, center of mass, and validity tolerances in all seven cases for FreeCAD 1.1.1 / SMLib 0.3.4. Exact topology counts are reported separately because the kernels may split the same geometry differently. See [the comparison report](../reports/geometry-benchmark/comparison.md) and its JSON reports.
- The native C++/Qt 6 application builds and runs without FreeCAD. Its current Design slice creates an exact OCCT solid, shows it in the native viewport, and provides resizable Viewport / Outliner / Properties editors, selection, camera navigation, and a previewable Move command. The native migration document records the remaining work.

## Checks run

- `make check` — 16 tests run, 15 pass, and the optional real SMLib integration test is skipped when smlibpy is absent from the default Python environment. The SMLib-enabled suite passes all 16 tests, including STEP and USD B-Rep round trips.
- `make freecad-check` — project save/reopen and OCCT geometry operations pass in the installed FreeCADCmd 1.1.1 baseline.
- `scripts/check-freecad-core.sh` against the fork's FreeCAD 1.1.3 FreeCADCmd — geometry operations and project save/reopen pass.
- `scripts/check-freecad-gui.sh` against the fork's FreeCAD 1.1.3 GUI — workbench registration, all three docks, project metadata, and a valid Part box fitted into the central viewport pass under Xvfb with software OpenGL. This verifies the GUI viewport path; pointer-driven navigation and hardware-GPU behavior have not been separately tested.
- The Release build compiled all targets from the FreeCAD fork's existing configured build tree and installed into a temporary prefix. A fresh configure has not been confirmed: rerunning CMake with the current Python 3.14 interpreter stops in the Pivy compatibility check because that interpreter cannot import Pivy. No system package or user installation was changed.
- `make native-check`, `make native-qml-smoke`, and the native Xvfb frame smoke pass. The native app's solid and editor layout were visually inspected; pointer-level interaction automation is still pending.
- `make build` builds the Python wheel. `make geometry-benchmark` completes the seven OCCT / SMLib comparisons within the documented measurement tolerances.

## Remaining work

- Repeat the FreeCAD 1.1.3 configure and build from an empty build directory with matching Pivy, yaml-cpp, and other development dependencies, so the pinned fork's build is reproducible from a clean checkout.
- Add automated pointer-level checks for the native viewport and modal move workflow.
- Continue replacing FreeCAD-owned document, selection, command, and persistence behavior with tested native equivalents before retiring the compatibility workbench.

## Existing machine-part study

The repository currently contains no `.FCStd`, STEP, IGES, or SMLib part files for the requested real machine-part comparison. Benchmark fixtures are regression geometry only; they do not alter engineering dimensions. The machine-part study remains pending until the actual part files are available in the workspace.
