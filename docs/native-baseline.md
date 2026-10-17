# Native Architecture Baseline Before Further Engineering Work

Recorded on 2026-10-17 at repository commit `41d4ef5` (`Document and guard
native architecture boundaries`). Checks ran from a clean detached worktree at
that commit, before the in-progress engineering-model changes in the main
checkout. This records the baseline for the renderer, picking, workspace,
licensing, native-independence, and FreeCAD-compatibility boundaries.

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

## Remaining manual gate

Run `make native-smoke` and `make native-pointer-smoke` on a desktop host where
the Qt XCB runtime, including `xcb-cursor0`, can create a visible window. Confirm
viewport rendering and the pointer-driven selection, camera, Move cancel/commit,
undo, and redo workflow there. Hardware-GPU verification also remains open;
software or offscreen rendering is not evidence of GPU behavior.

## Scope

This is a local verification snapshot, not a release gate. It verifies the
native model and persistence tests, Python compatibility layer, FreeCAD core
smoke, QML initialization, package build, geometry benchmark's OCCT cases, and
native binary independence. It does not establish an interactive frame in this
environment, an XTest pointer-level workflow, SMLib benchmark results, a clean
FreeCAD fork build, or hardware-GPU behavior.
