# Native structural field display

The viewport offers Geometry, Displacement, von Mises Stress, and Strain modes
for a selected CAD object with a converged structural run. The exact OCCT B-Rep
is retained as the nominal part. Result modes draw a separate, disposable
surface mesh reconstructed from the run's persisted hexahedral mesh snapshot.
Interior element faces are removed by shared-face incidence, so the viewport
shows the exterior boundary only.

Field arrays remain in their solver units and retain their mesh revision and
solver-run IDs. The legend uses display units:

- Displacement colors the nodal displacement magnitude in millimetres.
- von Mises Stress converts element-centred stress from Pa to MPa.
- Strain colors the element-centred equivalent strain in microstrain, using
  `sqrt(2/3 * (dev:dev))` for the stored symmetric tensor components.

The deformation control applies
`display_position = nominal_position + scale * displacement`. It defaults to
`1×` (physical scale), and the active multiplier is shown beside the legend.
The result mesh uses the source mesh coordinates and displacement field; it does
not change the OCCT shape or manufacturing model.

Before displaying a stored result, the view model checks that its material
definition still exists and remeshes the current CAD object using the run's
recorded element size. It compares persistent face references, node positions,
and element connectivity with the saved snapshot. A mismatch disables field
display and asks for a new solve. Schema-seven results remain loadable, but they
do not contain mesh coordinates/connectivity and cannot be visualized until
they are rerun.

The first mesher supports one axis-aligned rectangular OCCT solid and 8-node
hexahedra. The color range is the finite min/max of the selected result field;
uniform fields map to the midpoint color. Hardware-GPU rendering has not been
verified by this software-rendered build.
