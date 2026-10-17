# DatumLoop DS Milestone Status

**Milestone 1 is in progress; its release gate is not met.** The kernel
abstraction, OCCT adapter, optional SMLib adapter, and cross-kernel fixture
suite are implemented. The branded FreeCAD 1.1.3 build and a real desktop
OpenGL viewport check still need verification.

## Milestone 1 delivery

- The public [`ASI777/datumloopds`](https://github.com/ASI777/datumloopds)
  repository contains the DatumLoop DS workbench, native `.FCStd` project
  shell, and engineering services.
- The public [`ASI777/FreeCAD`](https://github.com/ASI777/FreeCAD) fork has a
  1.1.3-based `codex/datumloop-ds-1.1.3` branch with application branding,
  DatumLoop artwork, a desktop entry, and
  `StartWorkbench=DatumLoopWorkbench`. Its `ExeName=DatumLoopDS` gives the app
  separate settings and user-data paths while retaining FreeCAD's OCCT engine.
- The workbench installer targets the FreeCAD 1.1 profile at
  `$XDG_DATA_HOME/DatumLoopDS/v1-1/Mod` by default.
- The public [`ASI777/smlibpy`](https://github.com/ASI777/smlibpy) fork is
  available and currently exposes the upstream-compatible smlibpy 0.3.4 API.

## Geometry implementation

- `datumloopds.geometry` defines portable shape/value types, capabilities,
  typed unsupported-operation errors, and the `GeometryKernel` contract.
- The OCCT adapter calls FreeCAD `Part` operations and remains the production
  CAD path. A FreeCADCmd smoke exercises box, cylinder, extrusion, revolution,
  sweep, loft, fuse, cut, fillet, validity, bounding boxes, and tessellation.
- The optional SMLib adapter maps to the inspected smlibpy API directly. It
  supports primitives, features, exact Booleans, fillet/chamfer, shell/offset,
  measures, tessellation, STEP, native `.smb`, and text USD `BrepArray`. It does
  not route unsupported work to OCCT.
- Exact cross-kernel conversion goes through STEP. Tessellation remains limited
  to display and analysis.
- The deterministic seven-case benchmark builds equivalent geometry in both
  runtimes and records topology, measurements, timing, triangle count, and STEP
  round-trip. Its comparison JSON and Markdown also include the complete
  capability maps. The current FreeCAD 1.1.1 / SMLib 0.3.4 report matched
  volume, area, bounds, center-of-mass, and validity tolerances in all seven
  cases. Exact topology counts are reported separately because the kernels
  often represent the same geometry with different face and edge splits; see
  [`reports/geometry-benchmark/comparison.md`](../reports/geometry-benchmark/comparison.md)
  and the JSON reports beside it.

## Checks run

- `make check` — 16 tests run, 15 pass, and the optional real SMLib test is
  skipped when smlibpy is not installed in the default Python environment.
- SMLib-enabled full suite — all 16 tests pass against the pinned smlibpy
  0.3.4 wheel, including STEP and USD B-Rep round-trips.
- `make freecad-check` — project save/reopen and real OCCT geometry operations
  pass in FreeCADCmd 1.1.1.
- `make build` — builds the `datumloopds` wheel.
- `make geometry-benchmark` — the seven OCCT/SMLib cases complete, and the
  comparison currently reports 7/7 within volume, area, bounds, center-of-mass,
  and validity tolerances. Capability maps and exact topology counts are in the
  reports. The benchmark was run with the pinned smlibpy 0.3.4 package.
- A clean FreeCAD 1.1.3 configure was attempted from the fork branch. It stops
  at `find_package(yaml-cpp)` because the environment lacks the yaml-cpp CMake
  package configuration; no branded 1.1.3 build was produced.
- The available desktop session has no usable display/OpenGL context. The
  automated FreeCADCmd check does not claim GUI viewport verification.

The FreeCAD checks described above ran against 1.1.1. That is useful development
evidence, but it does not substitute for the pinned 1.1.3 release check.

## Remaining release gate

- Configure, build, and install `ASI777/FreeCAD` branch
  `codex/datumloop-ds-1.1.3` in an environment with FreeCAD's complete
  development dependencies, including yaml-cpp.
- Launch that build in a desktop session with a working OpenGL context. Verify
  the DatumLoop DS application identity, its isolated settings/profile, the
  workbench, and the central CAD viewport.

## Existing machine-part study

The current DatumLoop DS repository contains no `.FCStd`, STEP, IGES, or SMLib
part files to use for the requested real machine-part comparison. The toy
fixtures are confined to the benchmark suite and do not modify engineering
dimensions. Phase 6 remains pending until the existing DatumLoop part files are
available in the workspace.
