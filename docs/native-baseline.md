# Native Architecture Recheck Before Further Changes

Recorded on 2026-10-17 at repository commit `8259f7a` (`Add native parametric
feature graph`), before the verification and documentation updates in this
work slice. The checkout was clean before running the checks.

## Results

| Check | Result | Evidence and scope |
|---|---|---|
| `make native-check` | Pass | CMake built the application and all three native CTest targets; CTest passed `datumloop-core-tests`, `datumloop-io-tests`, and `datumloop-ui-tests` (3/3). |
| `make check` | Pass with optional skip | 18 Python tests ran: 17 passed and the SMLib integration test was skipped because `smlibpy` is not installed in the default Python environment. |
| `make freecad-check` | Pass | Local FreeCAD 1.1.1 passed the OCCT geometry, workbench, panel, viewport-slot, and project round-trip smoke. This does not verify a clean build of the pinned FreeCAD 1.1.3 fork. |
| `make native-independence-check` | Pass | No FreeCAD, Coin, or SoQt dynamic dependencies or native include bindings were found. |
| `make native-qml-smoke` | Pass with renderer limitation | The native root, workspace, three editors, exact OCCT box, and display mesh initialized without FreeCAD. Qt reported that the offscreen backend cannot present Qt Quick 3D frames; this check did not verify rendering. |
| `make build` | Pass | Built `datumloopds-0.1.0-py3-none-any.whl`. |
| `make geometry-benchmark` | Partial; command failed | The OCCT benchmark cases completed. The comparison stopped when the optional SMLib benchmark could not import `smlibpy`; the default Python test suite records the same optional dependency as skipped. |
| `make native-smoke` | Not available in this session | The app could not start because this session has no usable desktop display and its Qt XCB platform plugin lacks `xcb-cursor0`. No frame result was obtained. |
| `make native-pointer-smoke` | Not available in this session | Xvfb started, but Qt could not initialize the XCB plugin because `xcb-cursor0` is unavailable. The XTest pointer workflow did not run. |

The pointer smoke script uses XTest pointer and key events against the running
application; it is real interaction coverage when the XCB runtime dependencies
are present. This recheck does not claim pointer-level coverage. The earlier
software-rendered Xvfb result remains recorded in the milestone history, but
hardware-GPU behavior is unverified.

## Scope

This is a local verification snapshot, not a release gate. It verifies the
native model and persistence tests, Python compatibility layer, FreeCAD core
smoke, QML initialization, package build, and native binary independence. It
does not establish an interactive frame in this environment, optional SMLib
benchmark results, a clean FreeCAD fork build, or hardware-GPU behavior.
Qt's CMake QML import scan emitted plugin link-target scope warnings; the
native executable still built, and the QML initialization smoke passed.
