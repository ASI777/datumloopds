# Architecture and Milestone 1

## Application split

DatumLoop DS has three layers:

1. A branded FreeCAD desktop build owns the application identity, CAD document
   lifecycle, assembly editing, and 3D viewport.
2. The main DatumLoop DS repository owns the product workbench, engineering
   model, orchestration, solver adapters, and product checks.
3. Solver processes run in isolated, pinned environments. They communicate
   through versioned JSON-RPC over standard input and output; they never mutate
   a live FreeCAD document.

The FreeCAD GUI thread owns document changes. Workers return outputs and
proposed changes; the GUI applies accepted changes as undoable transactions.

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
available. The gate still requires a branded FreeCAD 1.1.3 build and a desktop
GUI launch with a functioning viewport. Current local constraints are
recorded in the milestone report.
