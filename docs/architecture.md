# Architecture and Migration

## Application architecture

The standalone C++20 / Qt 6 application with direct OCCT APIs is DatumLoop DS's
primary architecture. It owns its native workspace, document objects, stable
object and feature IDs, hierarchy, typed scalar properties, selection,
commands, dirty tracking, topology references, and the provisional `.dld`
project format. An initial object-scoped feature DAG recomputes exact OCCT
primitive, translation, and Boolean outputs. Exact `TopoDS_Shape` values remain
authoritative; the viewport receives derived display triangles. The first
topology-signature matcher is in place, while reliable persistent naming
through arbitrary feature edits remains later work.

FreeCAD is retained as a compatibility subsystem for the existing Python
workbench and `.FCStd` projects. It owns document lifecycle, its center
viewport, GUI selection, and host undo on that path. The `datumloopds.geometry`
OCCT adapter wraps FreeCAD's `Part` API. Those Python modules do not form a
runtime dependency of the standalone executable. See the direct dependency
inventory in [`native-migration-dependencies.md`](native-migration-dependencies.md).

Solver processes run in isolated, pinned environments. They communicate through
versioned JSON-RPC over standard input and output; they never mutate a live
application document.

Each host keeps document mutation on its GUI thread. The native UI applies
accepted commands to the native document; the FreeCAD compatibility UI uses
undoable FreeCAD transactions. Geometry adapters operate on exact shapes only.
They do not create documents, touch a host GUI, or change undo/redo state.
Kernel-native Python values remain behind `GeometryShape`; portable bounds,
vectors, and tessellation values cross that adapter boundary.

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

The standalone application uses C++20, Qt 6 Quick/QML, and direct OCCT APIs.
`DatumDocument` owns each exact `TopoDS_Shape`, project, object and feature
UUIDs, object hierarchy, typed scalar properties, shape metrics, translation,
dirty and revision state, topology reference signatures, and an object-scoped
feature graph. `DldProjectStore` reads and writes the version-three `.dld` manifest
using Qt Core and OCCT; it persists exact B-Rep geometry and regenerates display
tessellation after load. Schema v1 and v2 projects migrate to v3. Feature-backed
objects persist their feature inputs and recompute exact shapes on load; direct
imported geometry remains stored as B-Rep data.
`DocumentViewModel` provides Save/Open,
resets history after a successful open, and exposes dirty state to the shell.
See [`native-project-format.md`](native-project-format.md) for the supported
fields and current limitations.

QML composes `DatumWorkspace`, `DatumArea`, reusable `DatumRegion` and
`DatumEditor` bases, and a common `EditorHost`. The Design area loads explicit
Outliner, Viewport, and Properties editors. The viewport implementation is
behind `IViewportRenderer`; its current `QtQuick3DRenderer` adapter forwards
only `TriangleMesh` display data. The exact B-Rep stays in the document and is
not reconstructed from that mesh. `PickingService` maps a renderer hit to the
selected document object or topology reference by persistent ID. The native
viewport supports Object, Face, Edge, and Vertex modes. A Qt Quick 3D local hit
point and normal are resolved against the exact OCCT shape, then matched to a
stored topology UUID and the current output feature when available; ambiguous
results remain unresolved. Large-model acceleration and distinct subshape
highlight geometry remain future work.

SMLib remains an optional secondary geometry backend. FreeCAD remains isolated
in the compatibility subsystem and is not linked into the standalone native
application. The initial topology matcher can leave changed or ambiguous
references unresolved. The first feature graph supports box, cylinder, sphere,
and cone primitives, translation, and union/cut/intersection Booleans within
one object. Feature graph replacement creates a new initial topology reference
set for that object's output; provenance-aware topology migration,
object-to-object references, sketch and manufacturing features, rotations, and
engineering objects remain future work.

```text
Qt Quick / QML workspace and editor components
    |
EditorHost -> Outliner / Viewport / Properties
    |
Commands, PickingService, and DocumentViewModel
    |
DatumDocument (stable IDs, exact TopoDS_Shape, placement)
    |                                      \
OCCT primary geometry                  optional SMLib adapter
    |
TriangleMesh -> IViewportRenderer -> Qt Quick 3D display adapter

FreeCAD Python workbench -> separate compatibility path
```

The native application provides the Design workspace, exact OCCT solids,
resizable Viewport/Outliner/Properties editors, stable-ID object/face/edge/vertex
selection, basic camera navigation, previewable Move with undo/redo, and
version-three `.dld` save/open with an initial recomputable feature graph. The
renderer boundary, display-mesh architecture check, native independence check,
and pointer-driven UI check are included.
Further editors and engineering workflows are later work.

### Current native implementation status

The current CMake build produces `DatumLoopDS`, creates an exact OCCT box,
derives its display triangles, and loads the decomposed Qt Quick Design
workspace without loading FreeCAD. Core, persistence, and UI-model checks cover
geometry measures, `.dld` round-trip and v1/v2 migration, typed properties,
hierarchy, dirty state, exact primitive/transform/Boolean recompute, feature
status and error reporting, stable-ID object and subshape picking,
preview/cancel/commit behavior, input validation, and move/feature-graph
undo/redo. The headless QML smoke checks the common host
and all three editor components. `make native-independence-check` finds no
FreeCAD, Coin, or SoQt dynamic dependencies or native include bindings. The
offscreen platform cannot render Qt Quick 3D; Xvfb presents the app and the
XTest pointer smoke checks viewport object/face picking, camera orbit and zoom,
and Move cancel/confirm/undo/redo. Hardware-GPU behavior remains unverified.
The manual native viewport and `.dld` verification procedure is in
[`native-verification-checklist.md`](native-verification-checklist.md).

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
  FreeCAD, Coin, SoQt, or PySide.
- The native document owns its stable IDs, geometry, commands, and version-three
  `.dld` persistence, including initial face, edge, and vertex reference
  signatures. The previous FreeCAD document model remains the compatibility
  path until native import/export and project workflows are verified.
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
