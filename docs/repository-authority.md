# Repository authority

As of 2026-10-17, `ASI777/datumloopds` on GitHub is the source of truth. Its
`main` branch is at `2b8f995989de850fcdc7dcda18cf12d329f0651d`.

The canonical development checkout is `/tmp/datumloopds-main`, remote
`https://github.com/ASI777/datumloopds.git`. Its `main` branch matches the exact
`origin/main` base above. The current CAE review branch is
`codex/cad-driven-3d-elasticity`, tracked by its matching origin branch and
attached to draft PR #1. At the start of the 3D ROM-to-robot observation task,
its HEAD was `0ed1f869d881cb9e6b68261c0f92c2d99924f77d`. Continue on that branch
so the existing review remains coherent; create isolated branches or
worktrees for independent tasks when needed.

At the time of the audit, `/home/atedge/Documents/ChatGPT/datumloopds` was a
preserved, separate snapshot. It had no remote and no commits (unborn `master`;
its files were untracked), and
its source differs materially from the canonical checkout. It also contains
local build products and user-specific `.agents`, `.aws`, and `.codex` data.
Leave that directory intact and do not use it as the development checkout.
Review any specific file from it before selectively importing useful changes.

The snapshot was compared with the canonical checkout while excluding Git
metadata, build outputs, Python caches, package metadata, and the user-specific
directories above. Differences include the application entry point, core and
UI model sources, and the absence of most current native engineering modules
and tests from the snapshot. No snapshot changes have been imported or removed.
