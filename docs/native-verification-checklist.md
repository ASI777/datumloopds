# Native application verification checklist

Use this checklist on a normal desktop session with the intended graphics
driver. Xvfb and software OpenGL checks are useful automated coverage, but do
not count as hardware-GPU verification.

Record the DatumLoop revision, operating system, GPU and driver, Qt version,
OCCT version, and graphics backend before running the checks.

## Interactive viewport and commands

- Launch `DatumLoopDS` without FreeCAD installed or on `PATH`; confirm the
  native window presents and the OCCT-derived solid is visible.
- Orbit, pan, and zoom. Select the solid in Object mode, then select a face,
  edge, and vertex. Confirm the Properties editor shows the subshape type and
  topology UUID while the Outliner retains the owning object.
- Save and reopen the project, then reselect the same unchanged subshape. Confirm
  its topology UUID is unchanged. Confirm an ambiguous match reports unresolved
  instead of selecting by an enumeration index.
- Start Move, preview each axis, enter a finite distance, cancel, then repeat
  and commit. Confirm undo and redo restore the displayed placement and
  property values.

## Native project round-trip

- Save a `.dld` project, close the application, and reopen it.
- Confirm the project name, selected-object placement, bounds, and volume in the
  UI. Compare object UUIDs, hierarchy, and typed properties in the saved
  manifest with the source project.
- Select another object and confirm the viewport can regenerate its display
  tessellation from its stored exact geometry.
- Modify a project and attempt to open another file. Confirm the unsaved-change
  prompt appears; cancel it and verify the current project remains open.
- Open a malformed `.dld` file and confirm the current document remains intact
  and the status bar reports the error.

## Result classification

Record headless logic tests, software-rendered frame tests, and hardware-GPU
interactive checks separately. A passing software-rendered smoke does not
establish hardware performance, driver stability, or large-assembly capacity.
