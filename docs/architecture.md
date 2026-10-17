# Architecture and Migration

## Current compatibility stack

The existing application shell runs as a FreeCAD workbench. FreeCAD currently
owns document lifecycle, the center viewport, `.FCStd` persistence, GUI
selection, and the host undo stack. The repository owns project metadata,
workbench panels, exact-geometry adapters, and engineering services. The
`datumloopds.geometry` OCCT adapter wraps FreeCAD's `Part` API; the optional
SMLib adapter is independent of FreeCAD. See the direct dependency inventory in
[`native-migration-dependencies.md`](native-migration-dependencies.md).

Solver processes run in isolated, pinned environments. They communicate through
versioned JSON-RPC over standard input and output; they never mutate a live
application document.

The FreeCAD GUI thread owns document changes. Workers return outputs and
proposed changes; the GUI applies accepted changes as undoable transactions.
Geometry adapters operate on exact shapes only. They do not create documents,
touch the FreeCAD GUI, or change undo/redo state. Kernel-native values remain
behind `GeometryShape`; portable bounds, vectors, and tessellation values cross
the adapter boundary.

## Geometry kernels

`GeometryKernel` defines primitive creation, extrusion, revolution, sweep, loft,
Booleans, fillet, chamfer, shell, offset, measures, validity, tessellation, and
file exchange. Each adapter publishes a `KernelCapabilities` value. Unsupported
calls raise `UnsupportedKernelOperation`; there is no implicit OCCT fallback
and no mesh conversion in exact modeling operations.

### OCCT compatibility adapter

The current Python OCCT adapter wraps FreeCAD's `Part` and `TopoShape` APIs. It
keeps existing FreeCAD documents, Part Design, assemblies, healing, STEP
import/export, and existing machine geometry on the compatibility path. It is
not the architecture owner for new native features.

### SMLib / smlibpy

The SMLib adapter targets the public smlibpy 0.3.4 API. The `ASI777/smlibpy`
fork currently contains that upstream-compatible API; the inspection found no
missing operation that needs a fork-only binding. SMLibpy provides `Box`,
`Cylinder`, `Sphere`, `Cone`, extrusion, revolution, sweep, loft, exact Booleans,
fillet/chamfer, shell/offset, validity, and shape measures. Its wrapper also
provides STEP in/out, lossless native `.smb` save/load, and text USD
`BrepArray` import/export. The USD support is text `.usda`/`.usd`; it is not a
general binary `.usdc` or `.usdz` reader/writer.

At the source boundary, NVIDIA's `usd-brep` submodule supplies SMLib, SM_API,
and the SmPyLib bindings. Smlibpy adds its Python modeling layer and C++ helpers
for exact mass-property integration, topology access, sweeps, adaptive
tessellation, and USD text conversion. `BrepHandle` owns the native C++ Brep;
the adapter holds the Python shape so the handle's lifetime is preserved.
Smlibpy raises `ModelingError`/`KernelError` with operation context rather than
silently switching to OCCT.

Cross-kernel conversion uses exact STEP export/import, never tessellation. A
STEP conversion can still alter face grouping, tolerances, or topology; callers
must compare the result against engineering tolerances and validate it.

## Cross-kernel benchmark

`make geometry-benchmark` constructs the same deterministic models under each
kernel in separate Python runtimes: a box, cylinder, drilled plate, filleted
block, revolved spindle, swept tube, and multi-Boolean bracket. It writes
per-kernel JSON and a comparison JSON/Markdown report. The comparator uses
relative volume and area tolerance `1e-4`, bounding-box tolerance `1e-4 mm`,
center-of-mass tolerance `1e-4 mm`, and validity checks. It records complete
backend capability maps, topology counts, operation and tessellation time,
triangle count, and STEP round-trip. The CAD fixtures are regression geometry
only; they do not change DatumLoop engineering models.

To include existing STEP parts in the same two-runtime report, supply named
inputs as a JSON array. Source files are read in place; the report stores their
basenames and measurements, not their source contents or absolute paths:

```sh
DATUMLOOPDS_STEP_FILES_JSON='[{"name":"rail_x","path":"/path/to/rail_x.step"}]' \
DATUMLOOPDS_BENCHMARK_DIR=/tmp/datumloopds-parts \
make geometry-benchmark
```

The OCCT adapter uses FreeCAD's optimal bounding-box calculation when available
to avoid loose bounds for trimmed periodic surfaces.

FreeCAD's tessellation interface exposes chord tolerance but no angular
tolerance, while SMLib accepts both. The report calls this out and treats
triangle counts as approximate display comparisons.

## Native target architecture

The standalone application is being built with C++20, Qt 6 Quick/QML, a
DatumLoop-owned workspace and editor model, and direct OCCT geometry APIs. The
native core will own documents, stable object identities, commands and undo,
selection, engineering metadata, and `.dld` persistence. The viewport renderer
will display tessellation derived from exact OCCT shapes; that render mesh will
not replace the exact B-Rep. SMLib remains an optional secondary geometry
backend. FreeCAD remains behind the compatibility/migration boundary until
native project and engineering workflows provide equivalent functionality.

```text
Qt Quick / QML
    |
DatumLoop workspace, editors, tools
    |
Commands and view models
    |
DatumLoop document and engineering core
    |
OCCT primary geometry API ---- SMLib optional adapter
    |
Renderer and solver jobs
```

The first implementation slice is limited to the Design workspace, a minimal
document with one exact OCCT solid, resizable Viewport/Outliner/Properties
editors, selection, basic camera navigation, and a previewable modal move
command. Further editors and engineering workflows follow only after this slice
has a runnable native executable and tests.

### Current native implementation status

The current CMake build produces `DatumLoopDS`, creates an exact OCCT box,
derives its display triangles, and loads the Qt Quick Design workspace without
loading FreeCAD. Core and UI-model tests cover geometry measures, stable object
IDs, preview/cancel/commit behavior, input validation, and move undo/redo. The
headless QML smoke loads the component tree and OCCT display data. The offscreen
platform cannot render Qt Quick 3D, so the frame smoke uses Xvfb; it presented
three frames and visual inspection confirmed the solid and editor layout. A
separate pointer-level interaction check is still pending.

## Legacy FreeCAD shell

The existing **DatumLoop DS** workbench remains available while the native
shell is built. Its left panel provides project actions, workflow navigation,
and a mechanism tree. The right panel provides selection properties and
engineering context. The lower panel reserves space for jobs, playback, plots,
and evidence. Project creation, open, and save currently use native FreeCAD
documents.

The project targets FreeCAD 1.1.3 for its compatibility shell. The fork's
configured Release build, command-line smoke, and GUI viewport smoke now pass
in the isolated development environment. Repeating the configure and build
from an empty build directory still needs a matching Pivy and yaml-cpp
development environment; see the milestone status report.

## Application identity

The [FreeCAD branding guide](https://github.com/FreeCAD/FreeCAD-documentation/blob/main/wiki/Branding.md)
places application-level identity in the executable startup configuration or
in a `branding.xml` installed alongside the application. That source change
belongs in the FreeCAD fork. The workbench provides its own menu name and icon,
but those do not complete the branded application gate by themselves.

## Compatibility decisions

- Keep the existing `.FCStd` workbench path available during migration.
- New native modules use Qt 6, C++20, and direct OCCT APIs; they do not link to
  FreeCAD or PySide.
- The native document will own its stable IDs, geometry, commands, and future
  `.dld` persistence. The previous FreeCAD document model remains the
  compatibility path until native import/export and project workflows are
  verified.
- Keep all solver work out of the GUI process. No simulation code is introduced
  in this shell slice.

## Compatibility release status

The public DatumLoop DS repository and the FreeCAD and smlibpy forks are
available. The FreeCAD 1.1.3 branding integration is on
ASI777/FreeCAD:codex/datumloop-ds-1.1.3; it retains the upstream CAD engine and
uses a DatumLoopDS executable identity for separate settings. The configured
fork build, command-line smoke, and software-rendered GUI viewport smoke pass.
This verifies the transitional compatibility shell, not completion of the
standalone native application. A clean configure remains open because the
current Python 3.14 interpreter cannot import Pivy during CMake's compatibility
check. Current native migration status is tracked in
[native-migration-dependencies.md](native-migration-dependencies.md).
