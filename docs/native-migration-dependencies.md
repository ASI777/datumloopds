# Native Application Migration

## Current state

The repository retains a Python workbench hosted by FreeCAD and a
backend-neutral Python geometry contract with FreeCAD `Part` and optional
SMLib adapters. This is the compatibility subsystem. The standalone C++20 / Qt
6 application with direct OCCT APIs is the primary architecture. The Python
suite has 20 tests: 19 pass in the default Python environment and the optional
SMLib integration test is skipped unless `smlibpy` is installed. The
FreeCADCmd smoke and seven-case OCCT/SMLib benchmark remain compatibility
evidence; they do not verify a standalone application.

The native target is a C++20/Qt 6 Quick/QML executable. OCCT is its primary
exact kernel, and SMLib remains optional. `DatumDocument` owns stable object and
feature UUIDs, hierarchy, typed properties, exact B-Rep shapes, topology
references, an initial object-scoped parametric feature graph, and separate
engineering/electronics models. The native `.dld` format is at schema 6,
writes exact B-Rep, feature definitions, versioned mechanism data, materials,
circuits, and PCB data, and migrates schema 1 through 5 projects. FreeCAD
`.FCStd` support remains isolated in the compatibility workbench; native
persistence does not replace that import and migration path.

The native executable currently configures and builds against Qt 6.10.2 and
OCCT 7.9.2. Its OCCT core and UI-model checks cover exact geometry,
stable-ID picking, and Move preview/cancel/commit/undo/redo. The QML shell
composes a `DatumWorkspace`, `DatumArea`, `EditorHost`, and the Outliner,
Viewport, and Properties components. `IViewportRenderer` isolates the
display-mesh handoff; `PickingService` resolves render hits against exact
geometry and persistent topology references. An offscreen run creates and
tessellates the exact OCCT box, but Qt Quick 3D does not render through Qt's
offscreen plugin. The native frame and XTest pointer smokes pass under Xvfb
with software rendering, including selection, camera controls, and Move
cancel/confirm/undo/redo. Hardware-GPU behavior remains unverified; see
[`native-baseline.md`](native-baseline.md).

The transitional FreeCAD 1.1.3 fork now completes a clean Release build from an
empty build directory and installs into an isolated prefix. `FreeCADCmd`
passes OCCT geometry and project save/reopen checks; the GUI smoke passes
workbench registration, docks, project metadata, and a visible viewport solid
under Xvfb with software OpenGL. The tested source revision, toolchain, build
options, runtime notes, and limits are recorded in the
[`FreeCAD 1.1.3 build profile`](freecad-1.1.3-build-profile.md). This is
compatibility-path evidence; it does not replace native application checks.

## Dependency inventory

| Current feature | Direct or host dependency | Current location | Native owner | Retirement condition |
|---|---|---|---|---|
| Application lifecycle and main window | `FreeCADGui`, FreeCAD executable, workbench registration | `src/datumloopds/freecad/workbench.py`, `shell.py`, `freecad/DatumLoopDS/` | Qt `QGuiApplication`, `QQmlApplicationEngine`, DatumLoop app shell | Native executable starts, loads QML/workspaces, and exits cleanly without a FreeCAD process |
| Main CAD viewport and scene | FreeCAD GUI viewport and its internal Coin3D/renderer stack | FreeCAD host; workbench deliberately retains the central view | Qt Quick 3D renderer fed by tessellation from exact OCCT B-Reps | Native viewport displays exact-kernel-derived geometry and passes viewport interaction checks |
| Project/document lifecycle | `FreeCAD` document API and `.FCStd` | `src/datumloopds/freecad/project_actions.py` | `DatumDocument`, typed objects, transactions, versioned `.dld` persistence | Native save/open round trips stable IDs, geometry, parameters, and metadata; legacy import/export path exists |
| Exact OCCT geometry | FreeCAD `Part`, `TopoShape`, and bundled OCCT bindings | `src/datumloopds/geometry/occt/backend.py`, `geometry/benchmark.py` | Direct C++ OCCT geometry API | Native geometry tests cover primitives, measures, validity, Booleans, STEP, and tessellation |
| Optional SMLib geometry | `smlibpy==0.3.4`; Python package and C++ bindings | `src/datumloopds/geometry/smlib/backend.py` | Optional `SmlibKernel` adapter | Native adapter is independently built and can be omitted without losing OCCT workflows |
| Workbench panels and properties | FreeCAD `FreeCADGui`, PySide widgets, host docking | `src/datumloopds/freecad/shell.py` | Qt Quick editor components, view models, typed properties | Selection, properties, sizing, and workspace state are verified in the native app |
| Selection and hover | `FreeCADGui.Selection` and FreeCAD object/subelement handles | `src/datumloopds/freecad/shell.py`, `tests/FreeCADWidgetSmoke.FCMacro` | `PickingService` resolves object hits and exact-geometry face, edge, and vertex hits to stable object and topology UUIDs | Continue rejecting ambiguous topology matches; preserve the FreeCAD selection adapter until legacy workflows and `.FCStd` migration are covered |
| Undo and document mutation | FreeCAD document transactions and GUI-thread document edits | FreeCAD host today; not yet a standalone feature API | DatumLoop commands and document transactions | Native command execute/undo/redo and preview/cancel tests pass without host calls |
| Project metadata | Standard-library Python; metadata currently stored on FreeCAD document objects | `src/datumloopds/project.py`, `freecad/project_actions.py` | Native document metadata and migrations | `.dld` metadata validates, migrates, and round trips in a headless core test |
| Geometry benchmark and reporting | FreeCADCmd for OCCT; separate optional SMLib runtime | `scripts/`, `reports/geometry-benchmark/` | Native C++ OCCT regression and performance checks, retaining the Python cross-kernel benchmark | Direct OCCT results are measured; compatibility comparison remains available where both backends are installed |
| Solver/job isolation | Equation and generic steady/transient solver contracts; isolated ngspice DC worker; educational FEM and robotics fixtures | `src/datumloop/solver/`, `knowledge/`, `numerics/`, and `robotics/` | DatumLoop job manager and replaceable external solver processes | Versioned requests/results, cancellation, immutable inputs, validated outputs, and revision-aware application as commands |

## Migration boundaries

- New native C++ and QML code must not import or link `FreeCAD`, `FreeCADGui`,
  `Part`, PySide, or `.FCStd` APIs.
- Keep `src/datumloopds/freecad/` and `freecad/DatumLoopDS/` as the legacy
  integration while its workflows have no tested native replacement.
- Keep SMLib optional and name its kernel owner explicitly for every shape.
- Use OCCT B-Reps for exact modeling. Renderer meshes are derived display data
  and never become the document's source geometry.
- Move live document mutation into DatumLoop commands and transactions. QML
  calls view models; it does not implement engineering algorithms or write
  document state directly.
- Do not remove `.FCStd` support until the native project format can import and
  export the existing files required by the machine engineering workflow.

## Current native slice

The current native slice builds `DatumLoopDS`, creates exact OCCT geometry,
persists it in versioned `.dld` projects, and recomputes its initial feature
graph. The Design workspace contains replaceable Outliner, Viewport, and
Properties editors. Selection drives the property panel through
`PickingService`; camera orbit/pan/zoom and frame-selected controls are native.
Move previews a numeric translation along X/Y/Z and either commits it as an
undoable command or cancels it without changing the document.

`DatumEngineeringModel` stores version-one mechanism definitions with stable
body/object IDs, geometry-source descriptors, sourced optional mass properties,
body-local frames, supported joint types, actuators, contacts, requirements,
legacy linear/rotary axes, and motors. The `.dld` schema-6 readiness checks
diagnose incomplete study inputs, but do not run a solver or check joint
constraint consistency. Mechanism editing and geometry import have no native
UI yet; see [`milestone-2-mechanism-definition.md`](milestone-2-mechanism-definition.md).

The native build and headless shell smoke have a repository-managed Ubuntu/APT
lock in [`native-build-environment.md`](native-build-environment.md). The
FreeCAD compatibility build remains dependent on its separately documented
host package profile.

This is a foundation, not a replacement for Part Design, assemblies, native
engineering semantics, or production simulation. The equation/FEM/RL examples
are verification fixtures, not general solver capabilities. The FreeCAD 1.1.3
compatibility shell has a clean build/install and passing command-line and GUI
smokes. Keep FreeCAD integration until native equivalents for the workflows in
scope are implemented and tested. The native XTest pointer smoke covers
viewport selection, camera orbit and zoom, and the Move cancel/confirm/undo/redo
path under Xvfb with software OpenGL. Hardware-GPU behavior remains
unverified.
