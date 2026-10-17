# Native Baseline Before Architecture Work

Recorded on 2026-10-17 at repository commit `30edd27` (`Verify branded FreeCAD
1.1.3 compatibility shell`) before changing the native application architecture.

## Results

| Check | Result | Evidence and scope |
|---|---|---|
| `make native-check` | Pass | CMake configured and built the application and both native test targets. CTest passed `datumloop-core-tests` and `datumloop-ui-tests` (2/2). |
| `make native-qml-smoke` | Pass with expected renderer limitation | The QML root loaded and initialized the OCCT box and its 12 display triangles without FreeCAD. Qt Quick 3D reported that the offscreen platform has no QRhi-backed 3D rendering; this check does not claim a visible frame. |
| `xvfb-run -a -s '-screen 0 1600x1000x24 +extension GLX' make native-smoke` | Pass | The native window presented 5 frames without FreeCAD. This is an Xvfb/software-rendered smoke, not a hardware-GPU or pointer-interaction check. |

## Baseline limitations

- CMake reports that several Qt QML plugin link targets are not available in
  the current build scope. The application still builds, loads its QML module,
  and presents frames in the Xvfb smoke; revisit these warnings when the Qt
  dependency/package configuration is formalized.
- The native GUI has no pointer-level automated interaction check yet.
- These checks do not prove a clean dependency installation, hardware GPU
  behavior, project persistence, persistent topology, or simulation capability.
