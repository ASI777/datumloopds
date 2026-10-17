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
dirty and revision state, topology reference signatures, an object-scoped
feature graph, and separate engineering and electronics models.
`DatumElectronicsModel` owns shared material definitions/states, circuits, and
PCB boards; PCB layers and material regions reference that same material
collection. `DldProjectStore` reads and writes the version-five `.dld` manifest
using Qt Core and OCCT; it persists exact B-Rep geometry and regenerates display
tessellation after load. Schema v1 through v4 projects migrate with an empty
electronics model, preserving supported engineering data where the older
schema allowed it. Feature-backed
objects persist their feature inputs and recompute exact shapes on load; direct
imported geometry remains stored as B-Rep data.
`DocumentViewModel` provides Save/Open,
resets history after a successful open, and exposes dirty state to the shell.
See [`native-project-format.md`](native-project-format.md) for the supported
fields and current limitations.

`Main.qml` composes the application shell, project dialogs, and shortcuts;
`DatumWorkspace` composes the header, `DatumArea`, and status bar. The area
contains replaceable `EditorHost` instances for the Outliner, Viewport, and
Properties editors. Each editor has its own QML component and can be replaced
through the common host without moving viewport or panel behavior back into
the application shell.

The viewport display handoff is behind `IViewportRenderer`. Its current
`QtQuick3DRenderer` adapter accepts only `TriangleMesh` and writes it to the
Qt Quick 3D geometry item. `DatumDocument` owns the exact `TopoDS_Shape`; the
render mesh is disposable and is neither persisted as CAD geometry nor used to
reconstruct a shape. `ViewportEditor` still owns Qt Quick 3D camera, input, and
render-hit extraction, so changing rendering backends will require replacing
that presentation component while keeping the document model and hit-resolution
service intact. This is an incremental renderer boundary, not a backend-neutral
scene graph yet.

PCB documents use a separate `PcbShapeBuilder` to produce a disposable OCCT
preview from native board, copper, pad, trace, via, and optional package
envelope data. This display compound is not the canonical PCB definition or a
manufacturing/analysis solid. The desktop accepts `--pcb FILE` to import one
Circuit JSON board or the documented subset of a KiCad `.kicad_pcb` file into
the native electronics model before constructing the viewport. Adapter limits
and preview assumptions are listed in
[`electronics-materials-multiphysics.md`](electronics-materials-multiphysics.md).

`PickingService` maps backend hits to persistent DatumLoop identities before
selection changes. It resolves object IDs directly and resolves face, edge,
and vertex hits against the exact OCCT shape, matching a stored topology UUID
and current output feature when available. Ambiguous or unsupported results
remain unresolved. The native viewport supports Object, Face, Edge, and Vertex
modes. Large-model acceleration and distinct subshape highlight geometry
remain future work.

`DatumEngineeringModel` currently stores mechanisms, linear and rotary datum
axes, and motors separately from CAD objects. `DatumElectronicsModel` stores
materials, circuit connectivity, and PCB stack-up/geometry using common
material references. Mechanisms reference CAD body
object IDs; axes record their base and moving objects, metre-valued origin, and
unit direction. Motors may reference their CAD representation and mounting
topology, plus mass, inertia, center of mass, torque/speed data, electrical and
thermal data, encoder, and supplier fields. All physical values use SI units;
missing optional motor properties remain unknown and are not inferred from CAD
or renderer meshes. Document validation checks identity collisions and object
and topology ownership. These initial axes are engineering references only:
they do not impose kinematic constraints, and the model has no joint solver yet.

SMLib remains an optional secondary geometry backend. FreeCAD remains isolated
in the compatibility subsystem and is not linked into the standalone native
application. The initial topology matcher can leave changed or ambiguous
references unresolved. The first feature graph supports box, cylinder, sphere,
and cone primitives, translation, and union/cut/intersection Booleans within
one object. Feature graph replacement creates a new initial topology reference
set for that object's output; provenance-aware topology migration,
object-to-object references, sketch and manufacturing features, rotations,
joints, and topology migration remain future work.

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

DatumElectronicsModel -> Circuit JSON / KiCad format adapters
                     -> PcbShapeBuilder -> OCCT preview geometry
                     -> ICircuitSolver -> isolated ngspice worker

EquationRegistry -> generic physics solver requests/results
                 -> analytical checks and one educational cantilever FEM
RobotModel -> backend-neutral observation/action contracts
           -> deterministic fixture environment -> MJCF text export
           -> simulated StateInterface / hardware writes disabled

FreeCAD Python workbench -> separate compatibility path
```

The native application provides the Design workspace, exact OCCT solids,
resizable Viewport/Outliner/Properties editors, stable-ID object/face/edge/vertex
selection, basic camera navigation, previewable Move with undo/redo, and
version-five `.dld` save/open with an initial recomputable feature graph,
mechanism/motor metadata, and native material/circuit/PCB models. Separate
engineering prototypes add an E0-E6 equation registry, generic solver
contracts, a verified 1D cantilever FEM example, a reduced beam response, and a
simulated-only one-axis robot/RL environment with MJCF text export. Those
prototypes are not persisted in `.dld` or exposed in the Qt workspace.
Renderer/display-mesh separation,
workspace/editor composition, picking-service
boundary, and native independence are guarded by architecture checks. An XTest
pointer smoke covers the visible viewport and Move workflow when the Qt XCB
runtime is available. The 2026-10-17 recheck could not start the XCB window
because `xcb-cursor0` is missing; the offscreen check only verifies QML and
viewport-data initialization. Hardware-GPU behavior remains unverified. See
[`native-baseline.md`](native-baseline.md) for this run's results.

The first electronics slice imports supported Circuit JSON and KiCad PCB
profiles, builds a display-only OCCT preview, and runs a small ngspice DC
resistor/source example in a worker process. Result power is only a thermal
source placeholder. The worker cannot mutate the document, and no thermal,
structural, CFD, EM, chemistry, metallurgy, or coupled solve is implemented.
See [`electronics-materials-multiphysics.md`](electronics-materials-multiphysics.md)
for the material, physics, coupling, digital-twin, format, and license plan.

Equation governance, analytical/FEM verification, a limited reduced beam
response, and the simulated-only robot/RL/control interfaces are documented in
[`engineering-equation-governance.md`](engineering-equation-governance.md),
[`numerical-verification.md`](numerical-verification.md),
[`multifidelity-simulation.md`](multifidelity-simulation.md),
[`robot-learning-architecture.md`](robot-learning-architecture.md), and
[`sim-to-real.md`](sim-to-real.md). These provide verified examples and
interfaces; they are not production machine solvers or deployment readiness.

### Current native implementation status

The current CMake build produces `DatumLoopDS`, creates an exact OCCT box,
derives its display triangles, and loads the decomposed Qt Quick Design
workspace without loading FreeCAD. Six CTest targets cover geometry measures,
`.dld` round-trip and v1-v4 migration, typed properties, hierarchy, dirty state,
exact primitive/transform/Boolean recompute, feature status and error
reporting, engineering/electronics model validation and persistence, stable-ID
object and subshape picking, move/feature-graph undo/redo, Circuit JSON/KiCad
imports and ngspice, equation/unit gates, analytical checks, cantilever mesh
convergence, reduced-response error, robotics timing/action contracts, MJCF
well-formedness, and disabled hardware writes. The headless QML smoke checks
the common host and all three editor components. `make native-independence-check` finds no
FreeCAD, Coin, or SoQt dynamic dependencies or native include bindings. The
offscreen platform cannot render Qt Quick 3D; this environment also lacks the
`xcb-cursor0` runtime needed for the Xvfb window and XTest pointer workflow.
Hardware-GPU behavior remains unverified.
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
- The native document owns its stable IDs, geometry, commands, and version-five
  `.dld` persistence, including initial face, edge, and vertex reference
  signatures. The previous FreeCAD document model remains the compatibility
  path until native import/export and project workflows are verified.
- Keep solver processes out of the GUI process. The current engineering slice
  adds equation/solver contracts and small analytical, FEM, and robotics
  fixtures; it does not add production multiphysics solvers to the application.

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
