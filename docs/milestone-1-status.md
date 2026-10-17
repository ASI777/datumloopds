# Milestone 1 Status

**State: in progress; the milestone gate is not met.**

## Delivered in the local workspace

- A FreeCAD workbench manifest and GUI entry point for **DatumLoop DS**.
- A shell with project actions and workflow navigation on the left, engineering
  context and selection properties on the right, and study tabs below the
  central CAD viewport.
- Native `.FCStd` project creation, opening, and saving. Project ID, schema
  version, creation time, and metadata are stored in the document.
- Source launcher, per-user installer, pinned Python build backend, GPL-3.0-or-
  later notices, and reproducible local checks.
- Public repositories are available at [`ASI777/datumloopds`](https://github.com/ASI777/datumloopds),
  [`ASI777/FreeCAD`](https://github.com/ASI777/FreeCAD), and
  [`ASI777/smlibpy`](https://github.com/ASI777/smlibpy). The FreeCAD and smlibpy
  repositories are full forks of their upstream projects, and the DatumLoop DS
  source has been published to its repository.

## Checks completed

- `make check` — 6 tests passed.
- `make freecad-check` — FreeCADCmd created and reopened a project, retained its
  ID, registered the workbench commands, and built the panels around the
  existing central viewport widget.
- `make build` — built `datumloopds-0.1.0-py3-none-any.whl`.
- The installer copied the workbench and package into a temporary FreeCAD
  module directory; FreeCADCmd loaded that installed copy and passed the same
  project and panel smoke check.
- Python compilation and text whitespace checks passed.

The FreeCAD checks ran against the installed **1.1.1** build. The project target
is **1.1.3**, so the pinned build remains unverified.

## Remaining gate items

- Add application branding and separate application settings to the FreeCAD
  fork, then build and launch that fork from a clean checkout.
- Build against the pinned FreeCAD 1.1.3 release and verify the branded window
  and functioning OpenGL CAD viewport in a desktop session. This sandbox has no
  usable display: Qt's offscreen platform cannot create FreeCAD's `QOpenGLWidget`,
  and the virtual X server cannot be reached from this sandbox.
