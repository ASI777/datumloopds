# Canonical Source and Development Workflow

## Authoritative repository

The public source of truth is [`ASI777/datumloopds`](https://github.com/ASI777/datumloopds),
with `main` as its integration branch. Before the CAD-driven elasticity work,
the canonical clone's `origin` URL was verified and its clean `main` commit
matched GitHub at `2b8f995989de850fcdc7dcda18cf12d329f0651d` (2026-10-17).
The native elasticity slice is tracked on `codex/cad-driven-3d-elasticity`,
based directly on that commit.

Any other local directory is a snapshot or working copy until its Git remote,
commit, and tree are compared with the canonical repository. Do not silently
promote unmatched files into another DatumLoop source tree. Preserve them and
reconcile useful changes through a reviewed branch.

## Branch and checkout rules

- Start each task branch from a verified, clean `main` checkout. Use the
  `codex/` prefix for agent task branches.
- Keep implementation and verification commits on the task branch. Integrate
  them into `main` only through the repository's normal review process.
- Do not edit a second source checkout in parallel. Keep build output separate
  from tracked source and do not use generated files as source of truth.
- If a local directory does not match the canonical commit, preserve it until
  its contents have been explicitly reviewed; do not delete or overwrite it.

## FreeCAD compatibility boundary

FreeCAD is a compatibility subsystem for `.FCStd` migration, regression and
reference checks, import/export assistance, and compatibility defects. New
engineering features belong in native DatumLoop. FreeCAD changes should stay
within the compatibility boundary; maintain its existing coverage without
extending it to obtain native engineering capabilities.

## Engineering scope

MFEM is the primary backend for the first native 3D structural problem. Do not
add another solver until a physical workload justifies it. The first explicit
CAD-to-analysis-mesh implementation supports one axis-aligned rectangular
OCCT solid. Its structured hex mesh remains derived analysis data; the exact
B-Rep remains authoritative. Results are numerical verification only until
they have been compared with measured hardware. Hardware command writes remain
disabled.
