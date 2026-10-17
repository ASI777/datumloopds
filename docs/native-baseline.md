# Native Architecture Baseline Before Further Engineering Work

Recorded on 2026-10-17 at repository commit `41d4ef5` (`Document and guard
native architecture boundaries`). Checks ran from a clean detached worktree at
that commit, before the in-progress engineering-model changes in the main
checkout. This records the historical baseline for the renderer, picking,
workspace, licensing, native-independence, and FreeCAD-compatibility
boundaries. A later recheck of the current main state is recorded below.

## Results

| Check | Result | Evidence and scope |
|---|---|---|
| `make native-check` | Pass | CMake built the application and all three native CTest targets; CTest passed `datumloop-core-tests`, `datumloop-io-tests`, and `datumloop-ui-tests` (3/3). Qt's QML import scan emitted plugin target-scope warnings, but the application and tests built successfully. |
| `make check` | Pass with optional skip | 20 Python tests ran: 19 passed and the SMLib integration test was skipped because `smlibpy` is not installed in the default Python environment. |
| `make freecad-check` | Pass | Local FreeCAD 1.1.1 passed the OCCT geometry, workbench, panel, viewport-slot, and project round-trip smoke. This does not verify a clean build of the pinned FreeCAD 1.1.3 fork. |
| `make native-independence-check` | Pass | No FreeCAD, Coin, or SoQt dynamic dependencies or native include bindings were found. |
| `make native-qml-smoke` | Pass with renderer limitation | The native root, workspace, three editors, exact OCCT box, and display mesh initialized without FreeCAD. Qt reported that the offscreen backend cannot present Qt Quick 3D frames; this check did not verify rendering. |
| `make build` | Pass | Built `datumloopds-0.1.0-py3-none-any.whl`. |
| `make geometry-benchmark` | Partial; command failed | OCCT benchmark cases completed. The comparison stopped because the optional SMLib benchmark could not import `smlibpy`; the default Python suite records the same optional dependency as skipped. |
| `make native-smoke` | Blocked by host display dependencies | The app could not connect to a display; Wayland initialization was denied and the XCB plugin could not load because `xcb-cursor0` is unavailable. No frame result was obtained. |
| `make native-pointer-smoke` | Blocked by host display dependencies | Xvfb started, but Qt could not initialize the XCB plugin because `xcb-cursor0` is unavailable. The XTest pointer workflow did not run. |

The pointer smoke uses XTest pointer and key events against the running
application; it is real interaction coverage when the XCB runtime dependencies
are present. This recheck does not claim pointer-level coverage. Hardware-GPU
behavior is unverified.

## Current recheck at `ef81120`

The native project build and full CTest run passed all 10 targets. The Python
compatibility suite passed 19 tests with one optional SMLib integration test
skipped. Xvfb frame and XTest pointer smokes passed with software OpenGL; the
pointer run exercised selection, camera controls, and Move cancel/confirm/undo/
redo. The FreeCAD 1.1.3 fork completed a clean Release build and isolated
install; its `FreeCADCmd` geometry/project smoke and GUI workbench/viewport
smoke passed. See [`freecad-1.1.3-build-profile.md`](freecad-1.1.3-build-profile.md)
for the FreeCAD build details. Hardware-GPU behavior remains unverified.

## Scope

This is a local verification snapshot, not a release gate. The historical
baseline above records the native model and persistence tests, Python
compatibility layer, FreeCAD core smoke, QML initialization, package build,
geometry benchmark's OCCT cases, and native binary independence at `41d4ef5`.
The current recheck adds interactive software-rendered frame and XTest evidence,
plus a clean FreeCAD fork build/install. Neither run establishes hardware-GPU
behavior or current SMLib benchmark results.
