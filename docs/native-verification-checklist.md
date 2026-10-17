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

## NVIDIA hardware viewport smoke

The checked-in GPU path is separate from normal CI because this host has no
NVIDIA device. Configure a Linux x86-64 runner with an NVIDIA driver, an
NVIDIA-backed X11 display, native CMake/Qt 6/OCCT build dependencies, XTest,
`nvidia-smi`, and `xrandr`. Register it with the labels `self-hosted`, `linux`,
`x64`, `nvidia`, and `x11`. The manual
[`DatumLoop NVIDIA GPU viewport smoke`](../.github/workflows/native-nvidia-gpu-smoke.yml)
workflow builds the desktop application and runs the same local command:

```sh
python3 scripts/native_gpu_smoke.py --report artifacts/native-gpu/report.json
```

The harness requests Qt Quick's OpenGL RHI backend and does not force Mesa
software rendering. It records the GPU and driver, Qt RHI API, OpenGL vendor,
renderer and version, display and window resolution, presented frame count,
warnings and errors. It drives the existing XTest face-selection and camera
workflow, then switches the viewport to a synthetic, explicitly labeled von
Mises field fixture and checks that the rendered viewport pixels change. The
captured CAD and field-overlay frames are uploaded with the report.

The field values in this smoke fixture exercise visualization only; they are
not solver output and must not be cited as engineering results. The workflow
only runs when explicitly dispatched on a configured NVIDIA runner, so missing
GPU infrastructure does not block normal native builds. On a machine without a
driver, use `--allow-missing-gpu` to record `status: skipped`. GPU rendering
remains unverified until a report records `status: passed` on NVIDIA hardware.
