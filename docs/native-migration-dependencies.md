# Native Application Migration

## Current state

The repository's baseline is a Python workbench hosted by FreeCAD, plus a
backend-neutral Python geometry contract with FreeCAD `Part` and optional
SMLib adapters. The existing suite has 16 tests: 15 pass in the default Python
environment and the optional SMLib integration test is skipped unless
`smlibpy` is installed. The prior FreeCADCmd smoke and seven-case OCCT/SMLib
benchmark remain compatibility evidence; they do not verify a standalone
application.

The native target is a C++20/Qt 6 Quick/QML executable. OCCT remains the primary
exact kernel. SMLib remains optional. Existing FreeCAD files and adapters stay
available while native document, geometry, selection, command, and persistence
paths are built and verified.

The native executable currently configures and builds against Qt 6.10.2 and
OCCT 7.9.2. Its OCCT core and UI-model test targets pass, as does the existing
Python suite. An offscreen run loads the QML tree and creates/tessellates the
exact OCCT box, but Qt Quick 3D does not render through Qt's offscreen plugin.
The frame smoke passed under Xvfb and visual inspection confirmed the exact
solid and three-editor layout. Pointer-level native interaction has not yet had
a separate GUI automation check.

The transitional FreeCAD 1.1.3 fork build completed from its configured build
tree. Its FreeCADCmd geometry/project smoke and the GUI smoke for workbench,
docks, project metadata, and a visible viewport solid pass under Xvfb with
software OpenGL. A clean FreeCAD configure remains unverified because CMake's
Pivy compatibility check cannot import Pivy with the current Python 3.14
interpreter; see the milestone status report.

## Dependency inventory

| Current feature | Direct or host dependency | Current location | Native owner | Retirement condition |
|---|---|---|---|---|
| Application lifecycle and main window | `FreeCADGui`, FreeCAD executable, workbench registration | `src/datumloopds/freecad/workbench.py`, `shell.py`, `freecad/DatumLoopDS/` | Qt `QGuiApplication`, `QQmlApplicationEngine`, DatumLoop app shell | Native executable starts, loads QML/workspaces, and exits cleanly without a FreeCAD process |
| Main CAD viewport and scene | FreeCAD GUI viewport and its internal Coin3D/renderer stack | FreeCAD host; workbench deliberately retains the central view | Qt Quick 3D renderer fed by tessellation from exact OCCT B-Reps | Native viewport displays exact-kernel-derived geometry and passes viewport interaction checks |
| Project/document lifecycle | `FreeCAD` document API and `.FCStd` | `src/datumloopds/freecad/project_actions.py` | `DatumDocument`, typed objects, transactions, versioned `.dld` persistence | Native save/open round trips stable IDs, geometry, parameters, and metadata; legacy import/export path exists |
| Exact OCCT geometry | FreeCAD `Part`, `TopoShape`, and bundled OCCT bindings | `src/datumloopds/geometry/occt/backend.py`, `geometry/benchmark.py` | Direct C++ OCCT geometry API | Native geometry tests cover primitives, measures, validity, Booleans, STEP, and tessellation |
| Optional SMLib geometry | `smlibpy==0.3.4`; Python package and C++ bindings | `src/datumloopds/geometry/smlib/backend.py` | Optional `SmlibKernel` adapter | Native adapter is independently built and can be omitted without losing OCCT workflows |
| Workbench panels and properties | FreeCAD `FreeCADGui`, PySide widgets, host docking | `src/datumloopds/freecad/shell.py` | Qt Quick editor components, view models, typed properties | Selection, properties, sizing, and workspace state are verified in the native app |
| Selection and hover | `FreeCADGui.Selection` and FreeCAD object/subelement handles | `src/datumloopds/freecad/shell.py`, `tests/FreeCADWidgetSmoke.FCMacro` | DatumLoop selection service with persistent object IDs and explicit subelement references | Object/face/edge selection resolves to stable DatumLoop identities and synchronizes editors |
| Undo and document mutation | FreeCAD document transactions and GUI-thread document edits | FreeCAD host today; not yet a standalone feature API | DatumLoop commands and document transactions | Native command execute/undo/redo and preview/cancel tests pass without host calls |
| Project metadata | Standard-library Python; metadata currently stored on FreeCAD document objects | `src/datumloopds/project.py`, `freecad/project_actions.py` | Native document metadata and migrations | `.dld` metadata validates, migrates, and round trips in a headless core test |
| Geometry benchmark and reporting | FreeCADCmd for OCCT; separate optional SMLib runtime | `scripts/`, `reports/geometry-benchmark/` | Native C++ OCCT regression and performance checks, retaining the Python cross-kernel benchmark | Direct OCCT results are measured; compatibility comparison remains available where both backends are installed |
| Solver/job isolation | No native solver implementation in the current shell | architecture decisions in `docs/architecture.md` | DatumLoop job manager and external solver processes | Versioned requests/results are validated and applied as undoable document commands |

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

## First controlled slice

The initial native slice establishes a buildable `DatumLoopDS` executable, a
DatumLoop design-token module, a minimal core document, exact OCCT box creation,
and an OCCT-derived viewport mesh. The Design workspace contains resizable CAD
Viewport, Engineering Outliner, and Properties editors. Selection drives the
property panel. Camera orbit/pan/zoom and frame-selected controls are native.
The first Move command previews a numeric translation along X/Y/Z and either
commits it as an undoable command or cancels it without changing the document.

This is a foundation, not a replacement for Part Design, assemblies, project
migration, engineering metadata, or simulation. The FreeCAD 1.1.3 compatibility
shell's configured build and GUI smoke now pass, but a clean configure remains
open. Keep FreeCAD integration until native equivalents for the workflows in
scope are implemented and tested. Pointer-level native GUI interaction still
needs an automated check.
