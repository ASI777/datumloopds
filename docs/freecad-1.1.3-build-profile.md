# FreeCAD 1.1.3 clean build profile

This document records the clean compatibility-fork build and smoke evidence
used by the DatumLoop DS milestone. The build preserves FreeCAD's CAD engine
and adds the DatumLoop DS application branding.

## Source and toolchain

- Source: public `ASI777/FreeCAD` fork, branch `codex/datumloop-ds-1.1.3`.
- Tested source commit: `b6b11c9cabeeee14fd192856f5749af620ea7183`.
- Upstream release base: FreeCAD 1.1.3.
- CMake 4.2.3 and GCC 15.2.0.
- Qt 6.10.2 and OCCT 7.9.2.
- Python 3.14.4, Pivy 0.6.10, Coin/SIM Coin 4.0.6, Boost 1.90.0, and VTK 9.5.2.
- The build found PySide tooling 6.11.1; the tested runtime used matching
  PySide6/Shiboken 6.10.2 with Qt 6.10.2.

The fork retains upstream `pixi.toml` and `pixi.lock` as environment inputs.
They do not pin every compiler, system library, or host package in this build.

## Build profile

The successful build started from an empty build directory and used Release
configuration, GUI enabled, tests disabled, `INSTALL_TO_SITEPACKAGES=OFF`, and
an isolated install prefix. CMake reported a missing `swig_runtime_check.h`
compatibility header warning but configured and completed the build. Some
upstream developer test targets were still built with
`ENABLE_DEVELOPER_TESTS=ON`; the product build and install both completed
successfully.

The equivalent local build sequence is:

```sh
cmake -S . -B build-datumloop -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_GUI=ON -DBUILD_TEST=OFF -DBUILD_TESTING=OFF \
  -DENABLE_DEVELOPER_TESTS=ON -DINSTALL_TO_SITEPACKAGES=OFF \
  -DCMAKE_INSTALL_PREFIX="$PWD/install-datumloop"
cmake --build build-datumloop --parallel 2
cmake --install build-datumloop
```

The tested full build reached 100% and `cmake --install` exited successfully.
The installed `FreeCADCmd` reports `DatumLoopDS 1.1.3` and uses the branded
DatumLoopDS profile. Runtime imports must use PySide6/Shiboken that match the
installed Qt runtime; an incompatible user-site Qt binding can override the
matching runtime when launching from a development shell.

## Smoke evidence

- `scripts/check-freecad-core.sh` passed OCCT geometry operations and measures,
  workbench/panel initialization, the central viewport slot, and `.FCStd`
  project save/reopen.
- `scripts/check-freecad-gui.sh` passed workbench registration, dock creation,
  project metadata, and visible viewport-solid rendering under Xvfb with
  software OpenGL.
- The GUI screenshot was inspected and showed the project navigator, engineering
  panel, Studies dock, and rendered Part box.

These checks verify the FreeCAD compatibility fork in a software-rendered
environment. They do not verify hardware-GPU behavior or complete the separate
standalone native application milestone.
