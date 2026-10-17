# Architecture and Milestone 1

## Application split

DatumLoop DS has four layers:

1. A branded FreeCAD desktop build owns the application identity, CAD document
   lifecycle, assembly editing, and 3D viewport.
2. The main DatumLoop DS repository owns the product workbench, engineering
   model, orchestration, solver adapters, and product checks.
3. Solver processes run in isolated, pinned environments. They communicate
   through versioned JSON-RPC over standard input and output; they never mutate
   a live FreeCAD document.
4. Engineering geometry services call the backend-neutral
   `datumloopds.geometry` API. FreeCAD/OCCT is the production backend; SMLib is
   an optional exact-geometry backend for experiments and USD B-Rep workflows.

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

### OCCT / FreeCAD

The OCCT adapter wraps FreeCAD's `Part` and `TopoShape` APIs. It is the reference
implementation and keeps FreeCAD documents, Part Design, assemblies, healing,
STEP import/export, and existing machine geometry on the production path.
`.FCStd` project compatibility is not changed by the geometry abstraction.

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

## Milestone 1 scope

The first shell adds a dedicated **DatumLoop DS** workbench while keeping the
FreeCAD viewport in the center. The left panel provides project actions,
workflow navigation, and a mechanism tree. The right panel provides selection
properties and engineering context. The lower panel reserves space for jobs,
playback, plots, and evidence. Project creation, open, and save use native
FreeCAD documents.

The project targets FreeCAD 1.1.3. The local workbench can be exercised against
an installed FreeCAD copy for development, but it does not substitute for a
clean build of the pinned fork.

## Application identity

The [FreeCAD branding guide](https://github.com/FreeCAD/FreeCAD-documentation/blob/main/wiki/Branding.md)
places application-level identity in the executable startup configuration or
in a `branding.xml` installed alongside the application. That source change
belongs in the FreeCAD fork. The workbench provides its own menu name and icon,
but those do not complete the branded application gate by themselves.

## Decisions

- Use standard `.FCStd` documents for the first project shell. The mechanism
  model and study manifests will be layered onto those documents in later
  milestones.
- Keep the product core dependency-free. The host supplies FreeCAD and PySide;
  the Python build backend is pinned in `requirements-build.lock`.
- Keep all solver work out of the GUI process. No simulation code is introduced
  in this shell milestone.

## Current gate status

The public DatumLoop DS repository and full FreeCAD and smlibpy forks are
available. The FreeCAD 1.1.3 branding integration is on
`ASI777/FreeCAD:codex/datumloop-ds-1.1.3`; it retains the upstream CAD engine and
uses `ExeName=DatumLoopDS` so its preferences and user data are separate. The
gate still requires a successful branded FreeCAD 1.1.3 build and a desktop GUI
launch with a functioning viewport. Current local constraints are recorded in
the milestone report.
