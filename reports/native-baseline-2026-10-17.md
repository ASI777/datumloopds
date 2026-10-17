# Native platform baseline — 2026-10-17

Baseline source revision: `4d85eee` (`Implement native workspace and renderer boundaries`).

| Check | Result | Notes |
|---|---|---|
| `make native-check` | Pass | Native executable built; CTest passed 2/2. CMake emitted existing Qt QML plugin-target warnings. |
| `make check` | Pass | 18 Python tests passed; the optional SMLib backend test was skipped because `smlibpy` is not installed. |
| `make native-qml-smoke` | Pass, limited | QML/workspace and OCCT display data initialized. The offscreen platform cannot present a Qt Quick 3D frame. |
| `make native-smoke` under Xvfb with software GL | Pass | Native window presented 3 frames without loading FreeCAD. |
| `make native-pointer-smoke` under Xvfb | Pass | Real pointer events exercised viewport selection, orbit/zoom, Move cancel/confirm, undo, and redo. |
| `make native-independence-check` | Pass | No FreeCAD, Coin, or SoQt dynamic dependencies or native include bindings. |
| `make freecad-check` | Pass | Compatibility smoke passed using the installed FreeCAD 1.1.1 runtime. |
| `make geometry-benchmark` | Partial | OCCT benchmark cases completed. The wrapper exited before comparison because optional `smlibpy` is unavailable. |

The pointer and frame checks require Xvfb and were run outside the restricted display sandbox. A headless QML initialization check is not evidence of rendered viewport output. Hardware GPU behavior was not exercised.
