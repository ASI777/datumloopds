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
collection. `DldProjectStore` reads and writes the version-eight `.dld` manifest
using Qt Core and OCCT; it persists exact B-Rep geometry and regenerates display
tessellation after load. Schema v1 through v8 projects migrate while preserving
supported engineering and electronics data where the older schema allowed it.
Feature-backed
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
modes. Structural results can be displayed as derived colored boundary meshes
for displacement magnitude, von Mises stress, and equivalent strain. The
field mesh snapshot is persisted with the run; the exact OCCT shape remains the
nominal CAD definition. Large-model acceleration and distinct subshape
highlight geometry remain future work.

`DatumEngineeringModel` stores schema-eight mechanism definitions and structural
study records separately
from CAD objects: stable body IDs and geometry-source references, sourced
optional mass properties, body-local joint frames, supported joint types,
actuators, contacts, requirements, legacy datum axes, and motors.
`DatumElectronicsModel` stores materials, circuit connectivity, and PCB
stack-up/geometry using common material references. All physical values use SI
units; missing optional values remain unknown and are not inferred from CAD or
renderer meshes. Document validation checks identity, object, feature, and
contact-face ownership. Study-readiness checks diagnose missing inputs but do
not execute or validate solver constraints. Mechanism editing, STEP/SMLibPy
source workflows, and mass-property calculation remain unimplemented; see
[`milestone-2-mechanism-definition.md`](milestone-2-mechanism-definition.md).

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

EquationRegistry -> ScientificTask routing and structured proposals
                 -> maturity-gated SolverRequest -> numerical adapter
                 -> analytical checks and educational cantilever FEM/MFEM
DatumDocument/DatumEngineeringModel -> persistent CAD-face boundary conditions
                                    -> OCCT rectangular-solid analysis mesh
                                    -> MFEM 3D linear elasticity -> field datasets/provenance
                                    -> bounded load/modulus response ROM -> held-out metrics
RobotModel -> backend-neutral observation/action contracts
           -> MJCF export -> MuJoCo rigid runtime state -> ROM-augmented observation
           -> simulated StateInterface / hardware writes disabled

FreeCAD Python workbench -> separate compatibility path
```

The native application provides the Design workspace, exact OCCT solids,
resizable Viewport/Outliner/Properties editors, stable-ID object/face/edge/vertex
selection, basic camera navigation, previewable Move with undo/redo, and
version-eight `.dld` save/open with an initial recomputable feature graph,
mechanism body/joint/actuator/contact/requirement data, motors, and native
material/circuit/PCB models, material sources/states, topology-linked static
structural studies, mesh provenance, and MFEM field results. The first 3D
structural path is bounded to one axis-aligned rectangular OCCT solid and
serial Q1 isotropic small-strain elasticity. It runs a four-resolution
cantilever check with force balance, section stress, energy, and field
provenance; it does not establish experimental validation. Separate
engineering prototypes retain the E0-E6 equation registry, generic solver
contracts, 1D cantilever reference, held-out reduced beam response,
provider-neutral scientific-agent routing, and simulated-only one-axis
robot/RL environment. A bounded static response ROM is fitted from the 3D
MFEM case; its validity region and held-out parameter metrics are recorded.
The ROM is not yet persisted in `.dld`; a first observation-level bridge adds
its static tip deflection to a verified MuJoCo rigid-state observation without
changing MuJoCo dynamics. Dynamic feedback and a full MuJoCo environment
adapter remain future work.
The exact exported MJCF runs in the MuJoCo runtime check. Equation,
scientific-agent, and robotics prototypes are not exposed in the Qt workspace.
The mechanism data foundation and its limits are described in
[`milestone-2-mechanism-definition.md`](milestone-2-mechanism-definition.md).
Renderer/display-mesh separation,
workspace/editor composition, picking-service
boundary, and native independence are guarded by architecture checks. An XTest
pointer smoke covers the visible viewport and Move workflow when the Qt XCB
runtime is available. The 2026-10-17 Xvfb recheck passes selection, camera, and
Move cancel/confirm/undo/redo with software OpenGL; offscreen checks still only
verify QML and viewport-data initialization. Hardware-GPU behavior remains
unverified. See
[`native-baseline.md`](native-baseline.md) for this run's results.

The first electronics slice imports supported Circuit JSON and KiCad PCB
profiles, builds a display-only OCCT preview, and runs a small ngspice DC
resistor/source example in a worker process. Result power is only a thermal
source placeholder. The worker cannot mutate the document. No general thermal,
structural, CFD, EM, chemistry, metallurgy, or coupled production solver is
implemented. The 3D static elasticity path is limited to one axis-aligned
rectangular solid and remains a numerical verification benchmark.
See [`electronics-materials-multiphysics.md`](electronics-materials-multiphysics.md)
for the material, physics, coupling, digital-twin, format, and license plan.

Equation governance, analytical/FEM verification, a limited reduced beam
response, and the simulated-only robot/RL/control interfaces are documented in
[`engineering-equation-governance.md`](engineering-equation-governance.md),
[`numerical-verification.md`](numerical-verification.md),
[`multifidelity-simulation.md`](multifidelity-simulation.md),
[`robot-learning-architecture.md`](robot-learning-architecture.md),
[`scientific-agent-routing.md`](scientific-agent-routing.md), and
[`sim-to-real.md`](sim-to-real.md). These provide verified examples and
interfaces; they are not production machine solvers or deployment readiness.

### Current native implementation status

The current CMake build produces `DatumLoopDS`, creates an exact OCCT box,
derives its display triangles, and loads the decomposed Qt Quick Design
workspace without loading FreeCAD. Ten CTest targets cover geometry measures,
`.dld` round-trip and v1-v5 migration, typed properties, hierarchy, dirty state,
exact primitive/transform/Boolean recompute, feature status and error
reporting, engineering/electronics model validation and persistence, stable-ID
object and subshape picking, move/feature-graph undo/redo, Circuit JSON/KiCad
imports and ngspice, equation/unit gates, analytical checks, cantilever mesh
convergence, MFEM cross-backend results, held-out ROM error, model evidence,
scientific-agent routing, robotics timing/action contracts, actual MJCF loading
and stepping in MuJoCo, and disabled hardware writes. The headless QML smoke checks
the common host and all three editor components. `make native-independence-check` finds no
FreeCAD, Coin, or SoQt dynamic dependencies or native include bindings. The
offscreen platform cannot render Qt Quick 3D; the Xvfb frame and XTest pointer
workflow pass with software OpenGL. Hardware-GPU behavior remains unverified.
The manual native viewport and `.dld` verification procedure is in
[`native-verification-checklist.md`](native-verification-checklist.md).

## FreeCAD compatibility subsystem

The existing **DatumLoop DS** workbench remains available for compatibility.
Its left panel provides project actions, workflow navigation,
and a mechanism tree. The right panel provides selection properties and
engineering context. The lower panel reserves space for jobs, playback, plots,
and evidence. Project creation, open, and save currently use native FreeCAD
documents.

FreeCAD is maintained for `.FCStd` migration, regression/reference checks,
import/export assistance, and compatibility defects. New engineering features
are implemented in native DatumLoop. The project targets FreeCAD 1.1.3 for
compatibility. Hosted run [39028748447](https://github.com/ASI777/datumloopds/actions/runs/39028748447)
built and installed the fork but failed during its core smoke. The smoke
wrapper now prints command output on failure, and the isolated install's
PySide wrapper path is included; a passing hosted compatibility run remains
pending. See the [FreeCAD build profile](freecad-1.1.3-build-profile.md) for
the tested toolchain and scope.
The canonical repository and branch policy is in
[`development-workflow.md`](development-workflow.md).

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
- The native document owns its stable IDs, geometry, commands, and version-six
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
fork build, clean install, command-line smoke, and software-rendered GUI
viewport smoke pass. This verifies the transitional compatibility shell, not
completion of the standalone native application. Current native migration
status is tracked in
[native-migration-dependencies.md](native-migration-dependencies.md).
