# Blender 5.2 LTS UX Architecture Reference

## Source baseline and clean-room scope

The source baseline is the official `blender-v5.2-release` branch from the
official [`blender/blender` GitHub mirror](https://github.com/blender/blender).
The inspected branch head on 2026-10-17 was commit
[`35fea4a8ceacb061155b49eb0aecda55a287ee67`](https://github.com/blender/blender/tree/35fea4a8ceacb061155b49eb0aecda55a287ee67).
The branch is the stable 5.2 LTS maintenance branch; the inspected files are
linked at that immutable revision:

- [Screen operators and area operations](https://github.com/blender/blender/blob/35fea4a8ceacb061155b49eb0aecda55a287ee67/source/blender/editors/screen/screen_ops.cc)
- [Window-manager event and modal-operator routing](https://github.com/blender/blender/blob/35fea4a8ceacb061155b49eb0aecda55a287ee67/source/blender/windowmanager/intern/wm_event_system.cc)
- [3D View navigation](https://github.com/blender/blender/blob/35fea4a8ceacb061155b49eb0aecda55a287ee67/source/blender/editors/space_view3d/view3d_navigate.cc)
- [5.2 LTS areas manual](https://docs.blender.org/manual/en/5.2/interface/window_system/areas.html)
- [5.2 LTS workspaces manual](https://docs.blender.org/manual/en/5.2/interface/window_system/workspaces.html)
- [5.2 LTS regions manual](https://docs.blender.org/manual/en/5.2/interface/window_system/regions.html)
- [5.2 LTS user-interface release notes](https://developer.blender.org/docs/release_notes/5.2/user_interface/)

This review extracts interaction and architecture ideas only. DatumLoop DS does
not copy Blender source, GPL classes, icons, themes, artwork, logos, or other
UI resources. Every listed counterpart is to be implemented independently in
DatumLoop C++/QML, with DatumLoop terminology, visual identity, exact CAD
semantics, and engineering validation.

## Patterns adopted for independent implementation

| Blender concept | Problem it solves | DatumLoop equivalent | How DatumLoop differs | Implementation owner/module | Licensing status |
|---|---|---|---|---|---|
| Workspaces | Keeps layouts task-focused without closing the project | Design, Assembly, Simulation, Manufacturing, Inspection, and later task workspaces | Each workspace is engineering-oriented and can bind to machine, solver, or inspection context | `src/datumloop/workspace/` | Independently implemented; no Blender source or UI resources used |
| Areas and split/join/resize | Lets users compose screen space around the current task | Resizable DatumArea layout with editor switching and temporary maximize/restore | Native Qt Quick splitters first; later explicit DatumLoop split/join commands; never imports Blender screen data | `src/datumloop/workspace/`, QML workspace components | Independently implemented; no Blender source or UI resources used |
| Editors and regions | Gives specialized tools a predictable main area and supporting controls | CAD Viewport, Engineering Outliner, Properties, Graph, Spreadsheet, Jobs, Evidence, and Console editors with DatumRegions | Regions expose engineering overlays, units, solver state, and validation status | `src/datumloop/editors/`, `qml/editors/` | Independently implemented; no Blender source or UI resources used |
| Headers, tool headers, sidebars, and status region | Keeps common editor actions near the work area and makes tool state visible | Editor header, context tool strip, engineering sidebar, and compact command/job status bar | Content describes dimensions, datums, material, tolerance, solver residuals, and units | `qml/components/`, `src/datumloop/ui/` | Independently implemented; no Blender source or UI resources used |
| Context-aware input and keymaps | Avoids running commands in the wrong editor or mode | `InputContext`, keymaps, command bindings, and tool bindings scoped by active workspace/editor/tool/selection | Shortcuts are CAD-specific; command preconditions check geometry and engineering state | `src/datumloop/commands/`, `src/datumloop/tools/`, `src/datumloop/input/` | Independently implemented; no Blender source or UI resources used |
| Modal tools and operator lifecycle | Supports fast multi-step actions with live feedback and cancel | Modal DatumTool states driven by pointer, axis constraints, numeric entry, confirm, and cancel | Preview stays separate from the committed document and is applied through an undoable command | `src/datumloop/tools/`, `src/datumloop/commands/` | Independently implemented; no Blender source or UI resources used |
| 3D View navigation and overlays | Makes spatial inspection fast and keeps view controls discoverable | Exact-CAD viewport with orbit, pan, zoom, frame, orthographic views, and independent engineering overlays | Overlay values are engineering data such as datums, clearances, COM, axes, tolerances, stress, and motion | `src/datumloop/viewport/`, `src/datumloop/renderer/` | Independently implemented; no Blender source or UI resources used |
| Gizmos | Makes transforms and spatial relationships directly manipulable | DatumGizmo/GizmoGroup for transforms, dimensions, joints, axes, sensors, and section planes | Gizmos encode mechanical constraints and units, not only generic transforms | `src/datumloop/gizmos/` | Independently implemented; no Blender source or UI resources used |
| Outliner tree navigation | Makes large hierarchies navigable and supports focus on active data | Engineering Outliner with Assembly, Geometry, Mechanisms, Simulation, Materials, and Dependencies views | Adds suppression, stale solve, missing reference, collision, tolerance, and manufacturing indicators | `src/datumloop/editors/outliner/` | Independently implemented; no Blender source or UI resources used |
| Properties editor | Keeps context-specific settings close to selected data | Typed engineering property panels generated from DatumLoop schemas | Includes ranges, units, expressions, evidence, provenance, validation, and read-only derived values | `src/datumloop/engineering/`, `qml/editors/properties/` | Independently implemented; no Blender source or UI resources used |
| Operator/command search | Helps users find actions without memorizing all menus | Universal search over commands, tools, workspaces, editors, assets, settings, and help | Results include applicability checks, required selection, units, and engineering warnings | `src/datumloop/commands/`, `src/datumloop/search/` | Independently implemented; no Blender source or UI resources used |
| Node editors and result viewers | Makes procedural and connected workflows inspectable | Typed Engineering Graph for geometry, mechanism, simulation, and optimization; viewer outputs to viewport/table/plot | Typed engineering sockets reject invalid material, unit, geometry, and solver connections | `src/datumloop/graph/`, `src/datumloop/jobs/` | Independently implemented; no Blender source or UI resources used |
| Spreadsheet/data inspection | Exposes structured results in a sortable, selectable form | Engineering Spreadsheet for mass, temperature, stress, tolerance, and motion data | Selection synchronizes with engineering objects; values carry units and run provenance | `src/datumloop/editors/spreadsheet/` | Independently implemented; no Blender source or UI resources used |
| Asset browser and shelves | Makes reusable content discoverable and fast to place | DatumLoop Component Library and contextual shelves for motors, bearings, rails, tools, fixtures, and sensors | A dropped asset creates an engineering entity with exact geometry, parameters, provenance, and supplier data | `src/datumloop/assets/`, `qml/editors/assets/` | Independently implemented; no Blender source or UI resources used |
| Context menus and drag/drop | Keeps local actions close and supports direct manipulation between editors | Typed context actions and validated drops between assets, viewport, assembly, graph, and properties | Drop targets validate engineering compatibility before a transaction commits | `src/datumloop/commands/`, `src/datumloop/ui/`, `src/datumloop/assets/` | Independently implemented; no Blender source or UI resources used |
| GPU abstraction | Separates drawing work from editor and data ownership | Renderer interface with OCCT-derived display data and explicit overlay passes | OCCT B-Reps stay authoritative; render meshes are disposable display artifacts | `src/datumloop/renderer/`, `src/datumloop/viewport/` | Independently implemented; no Blender source or UI resources used |

## Architecture observations

The 5.2 manual describes a workspace as a saved arrangement of areas and
editors, and explains that keyboard actions can depend on the editor under the
pointer. The source has separately registered area split/join operators with
invoke, modal, cancel, and poll callbacks, and the window manager routes keymap
and operator events through context-aware handlers. These are useful boundaries
to reproduce: a workspace owns layout; an editor owns its regions and input
context; commands expose explicit eligibility, preview, confirmation, and
cancellation. DatumLoop is not adopting Blender's data model, operator API,
screen storage, or implementation classes.

## Initial application implementation

The first native slice takes only the layout and interaction patterns that
directly support CAD: a task workspace, resizable editors, a viewport, an
engineering tree, selection-driven properties, a previewable Move command, and
live status feedback. Asset libraries, node graphs, spreadsheet, multi-view,
and solver timeline are later stages in the native migration plan.
