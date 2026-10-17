# DatumLoop DS Milestone Status

**The native application milestone proceeds independently of the FreeCAD compatibility gate.** DatumLoop's long-term direction is the standalone C++20 / Qt 6 / QML application; FreeCAD remains a migration and compatibility path until native workflows replace it. The FreeCAD 1.1.3 fork has now completed a clean Release build and isolated install; command-line geometry/project and software-rendered GUI smokes pass. See [Native Application Migration](native-migration-dependencies.md) and the [FreeCAD build profile](freecad-1.1.3-build-profile.md).

## Milestone 1 delivery

- The public [ASI777/datumloopds](https://github.com/ASI777/datumloopds) repository contains the compatibility workbench, project metadata, geometry adapters, engineering services, and the native Qt application slice.
- The public [ASI777/FreeCAD](https://github.com/ASI777/FreeCAD) fork has a FreeCAD 1.1.3-based `codex/datumloop-ds-1.1.3` branch with application branding, DatumLoop artwork, a desktop entry, and the DatumLoop start workbench. Its DatumLoopDS executable identity gives it a separate settings profile while retaining FreeCAD's CAD engine.
- The public [ASI777/smlibpy](https://github.com/ASI777/smlibpy) fork is available and currently exposes the upstream-compatible smlibpy 0.3.4 API.
- The compatibility workbench installer targets `$XDG_DATA_HOME/DatumLoopDS/v1-1/Mod` by default.
- The standalone native application and the FreeCAD 1.1.3 fork now have separate locked build images. The FreeCAD image pins its Ubuntu base digest and direct Qt 6 build package versions against the dated Ubuntu snapshot; CI builds the pinned fork commit from a clean checkout and runs project and software-rendered GUI smokes. See the [FreeCAD build profile](freecad-1.1.3-build-profile.md) and [native build environment](native-build-environment.md).

## Geometry and migration status

- The Python geometry contract defines portable shape/value types, capabilities, typed unsupported-operation errors, and backend adapters. Its FreeCAD Part adapter remains the compatibility CAD path. The native C++ application with direct OCCT APIs is the primary architecture; SMLib remains optional.
- Exact cross-kernel conversion goes through STEP. Tessellation remains a display and analysis representation, not an exact modeling source.
- The deterministic seven-case OCCT / SMLib benchmark matched volume, area, bounds, center of mass, and validity tolerances in all seven cases for FreeCAD 1.1.1 / SMLib 0.3.4. Exact topology counts are reported separately because the kernels may split the same geometry differently. See [the comparison report](../reports/geometry-benchmark/comparison.md) and its JSON reports.
- The native C++/Qt 6 application builds and runs without FreeCAD. Its Design slice creates exact OCCT solids, provides a renderer boundary and decomposed Viewport / Outliner / Properties editors, resolves object/face/edge/vertex picks through stable IDs, and supports camera navigation and previewable Move. The initial feature DAG evaluates box/cylinder/sphere/cone primitives, translation, and exact Boolean union/cut/intersection; generated `TopoDS_Shape` values remain authoritative and feature graph replacement is undoable. Native `.dld` schema 7 stores feature definitions, exact B-Rep results, the versioned mechanism-definition foundation, motors, sourced materials, circuits, PCB models, structural studies, mesh convergence, fields, and solver provenance; it reads v1-v6 projects and preserves supported prior engineering data. The topology matcher rejects ambiguous results and does not promise stable naming through arbitrary feature edits. Large-model acceleration and distinct subshape highlight geometry remain open.

## Checks run

- Native CMake build with MFEM 4.10 and the isolated MuJoCo 3.16.0 Python runtime configured — all 11 CTest targets pass. Coverage includes geometry/document/electronics/UI, Equation Registry and dimensional checks, model evidence and agent routing, the MFEM cantilever/ROM checks, four CAD-derived 3D elasticity meshes with a 5% convergence gate, and runtime load/step of the exact MJCF emitted by the C++ robotics exporter. The camera placeholder still returns no frame or pixels, and hardware actuator writes remain rejected.
- `make native-qml-smoke` — workspace, common host, three editors, and OCCT-derived mesh data initialize. Qt Quick 3D cannot render in the offscreen plugin, as expected.
- `make native-independence-check` — passes with no FreeCAD, Coin, or SoQt dynamic dependencies or native include bindings.
- `xvfb-run ... DatumLoopDS --smoke` — passes and presents five frames with software OpenGL. Hardware-GPU behavior is not covered.
- Native XTest pointer smoke — passes under Xvfb with software OpenGL, including object/face selection, camera controls, and Move cancel/confirm/undo/redo. Hardware-GPU behavior remains unverified; see [the baseline report](native-baseline.md).
- `make check` — 20 Python compatibility tests ran: 19 passed and the optional smlibpy integration test was skipped because smlibpy is not installed in the default Python environment.
- `make freecad-check` — compatibility smoke passed geometry operations and project save/reopen with locally installed FreeCAD 1.1.1. This is not a validation of the pinned FreeCAD 1.1.3 fork build.
- `scripts/check-freecad-core.sh` against the clean FreeCAD 1.1.3 install — OCCT operations and measures pass; workbench panels, central viewport slot, and `.FCStd` project save/reopen pass.
- `scripts/check-freecad-gui.sh` against the fork's FreeCAD 1.1.3 GUI — workbench registration, docks, project metadata, and a visible viewport solid pass under Xvfb with software OpenGL.
- A clean FreeCAD 1.1.3 Release configure/build reached 100%, and `cmake --install` completed into an isolated prefix. The tested versions, options, and evidence are recorded in the [FreeCAD build profile](freecad-1.1.3-build-profile.md).
- `make build` builds the Python wheel. With SMLib 0.3.4 installed, `make geometry-benchmark` previously completed the seven OCCT / SMLib comparisons within the documented measurement tolerances. In the current default Python environment, the OCCT cases pass but the comparison stops because the optional `smlibpy` package is absent; see [the baseline report](native-baseline.md).
- CMake still warns that some Qt QML plugin link targets are unavailable in the current scope; the application builds, loads its QML, and presents frames. The offscreen platform still cannot verify 3D rendering.

## Remaining work

- The native C++/Qt/OCCT application and FreeCAD 1.1.3 fork each have a
  repository-managed Ubuntu image, dated APT snapshot, and direct package
  lock. Hosted run [39028748447](https://github.com/ASI777/datumloopds/actions/runs/39028748447)
  built and installed the pinned FreeCAD fork, then failed during the FreeCAD
  core smoke. The smoke wrapper now preserves command output on failure and
  the clean build script supplies the install's PySide wrapper path; a passing
  hosted compatibility run remains pending.
- Verify rendering on a hardware GPU; the current viewport frame and pointer checks use Xvfb software OpenGL.
- Extend feature dependency editing and recompute invalidation; add provenance-aware topology migration, large-model acceleration, and subshape highlighting. The current signature matcher is only an initial strategy.
- FreeCAD is now limited to `.FCStd` migration, regression/reference checks, import/export assistance, and compatibility defects. Continue new engineering workflows in native DatumLoop.
- Resolve the license and contributor-ownership questions recorded in [`licensing-strategy.md`](licensing-strategy.md) before relicensing or selecting a proprietary distribution strategy. This decision does not block technical development.
- Continue replacing FreeCAD-owned document, selection, command, and persistence behavior with tested native equivalents before retiring the compatibility workbench.

## Existing machine-part study

The repository currently contains no `.FCStd`, STEP, IGES, or SMLib part files for the requested real machine-part comparison. Benchmark fixtures are regression geometry only; they do not alter engineering dimensions. The machine-part study remains pending until the actual part files are available in the workspace.
