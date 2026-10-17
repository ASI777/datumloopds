# DatumLoop DS Milestone Status

**Milestone 1 remains in progress because a clean FreeCAD fork configure and build has not yet been repeated from an empty build tree.** The configured FreeCAD 1.1.3 build completed, and its command-line and GUI smoke checks passed. The product's long-term direction is now the standalone C++20 / Qt 6 / QML application; FreeCAD remains a migration and compatibility path until native workflows replace it. See [Native Application Migration](native-migration-dependencies.md).

## Milestone 1 delivery

- The public [ASI777/datumloopds](https://github.com/ASI777/datumloopds) repository contains the compatibility workbench, project metadata, geometry adapters, engineering services, and the native Qt application slice.
- The public [ASI777/FreeCAD](https://github.com/ASI777/FreeCAD) fork has a FreeCAD 1.1.3-based `codex/datumloop-ds-1.1.3` branch with application branding, DatumLoop artwork, a desktop entry, and the DatumLoop start workbench. Its DatumLoopDS executable identity gives it a separate settings profile while retaining FreeCAD's CAD engine.
- The public [ASI777/smlibpy](https://github.com/ASI777/smlibpy) fork is available and currently exposes the upstream-compatible smlibpy 0.3.4 API.
- The compatibility workbench installer targets `$XDG_DATA_HOME/DatumLoopDS/v1-1/Mod` by default.

## Geometry and migration status

- The Python geometry contract defines portable shape/value types, capabilities, typed unsupported-operation errors, and backend adapters. Its FreeCAD Part adapter remains the compatibility CAD path. The native C++ application with direct OCCT APIs is the primary architecture; SMLib remains optional.
- Exact cross-kernel conversion goes through STEP. Tessellation remains a display and analysis representation, not an exact modeling source.
- The deterministic seven-case OCCT / SMLib benchmark matched volume, area, bounds, center of mass, and validity tolerances in all seven cases for FreeCAD 1.1.1 / SMLib 0.3.4. Exact topology counts are reported separately because the kernels may split the same geometry differently. See [the comparison report](../reports/geometry-benchmark/comparison.md) and its JSON reports.
- The native C++/Qt 6 application builds and runs without FreeCAD. Its Design slice creates an exact OCCT solid, provides a renderer boundary and decomposed Viewport / Outliner / Properties editors, resolves object picks through stable IDs, and supports camera navigation and a previewable Move command. Display triangles remain derived from the exact shape. Native `.dld` schema-v2 persistence now stores exact OCCT B-Reps and initial face, edge, and vertex references. The matcher rejects ambiguous results and does not promise stable naming through arbitrary feature edits. Interactive subshape picking remains unimplemented.

## Checks run

- `make native-check` — CMake builds the app and native test targets; CTest passes 3/3, including project I/O and topology-resolution coverage.
- `make native-qml-smoke` — workspace, common host, three editors, and OCCT-derived mesh data initialize. Qt Quick 3D cannot render in the offscreen plugin, as expected.
- `make native-independence-check` — passes with no FreeCAD, Coin, or SoQt dynamic dependencies or native include bindings.
- `xvfb-run ... DatumLoopDS --smoke` — passes and presents five frames with software OpenGL. Hardware-GPU behavior is not covered.
- `make native-pointer-smoke` — XTest clicks the viewport to select the solid, orbits and zooms the camera, then exercises Move cancel, confirm, undo, and redo.
- `make check` — 18 tests run: 17 pass and the optional smlibpy integration test is skipped because smlibpy is not installed in the default Python environment. The SMLib-enabled suite previously passed all 16 integration tests, including STEP and USD B-Rep round trips.
- `make freecad-check` — the compatibility smoke passes geometry operations and project save/reopen with the locally installed FreeCAD 1.1.1. This is not a validation of the pinned FreeCAD 1.1.3 fork build.
- `scripts/check-freecad-gui.sh` against the fork's FreeCAD 1.1.3 GUI — workbench registration, docks, project metadata, and a visible viewport solid pass under Xvfb with software OpenGL.
- The Release build compiled all targets from the FreeCAD fork's existing configured build tree and installed into a temporary prefix. A fresh configure has not been confirmed: rerunning CMake with the current Python 3.14 interpreter stops in the Pivy compatibility check because that interpreter cannot import Pivy. No system package or user installation was changed.
- `make build` builds the Python wheel. `make geometry-benchmark` completes the seven OCCT / SMLib comparisons within the documented measurement tolerances.
- CMake still warns that some Qt QML plugin link targets are unavailable in the current scope; the application builds, loads its QML, and presents frames. The offscreen platform still cannot verify 3D rendering.

## Remaining work

- Repeat the FreeCAD 1.1.3 configure and build from an empty build directory with matching Pivy, yaml-cpp, and other development dependencies, so the pinned fork's build is reproducible from a clean checkout.
- Verify rendering on a hardware GPU; the current viewport frame and pointer checks use Xvfb software OpenGL.
- Extend topology resolution with feature provenance and subshape picking; the current signature matcher is only an initial strategy.
- Continue native engineering workflows before replacing corresponding compatibility paths.
- Resolve the license and contributor-ownership questions recorded in [`licensing-strategy.md`](licensing-strategy.md) before relicensing or selecting a proprietary distribution strategy. This decision does not block technical development.
- Continue replacing FreeCAD-owned document, selection, command, and persistence behavior with tested native equivalents before retiring the compatibility workbench.

## Existing machine-part study

The repository currently contains no `.FCStd`, STEP, IGES, or SMLib part files for the requested real machine-part comparison. Benchmark fixtures are regression geometry only; they do not alter engineering dimensions. The machine-part study remains pending until the actual part files are available in the workspace.
