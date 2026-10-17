# Cross-kernel geometry benchmark

OCCT / FreeCAD **1.1.1** · SMLib **0.3.4**

The suite uses the same dimensions and exact operations in both kernels. Geometry match uses relative volume and surface-area tolerances of 1e-4, absolute bounding-box and center-of-mass tolerances of 1e-4 mm, and valid B-Reps in both results. Exact topology counts are reported as a separate comparison because equivalent kernels can split faces differently. Timings are measurements from the current machine and are not expected to repeat exactly.

## Capability comparison

| Capability | OCCT | SMLib |
|---|---:|---:|
| boolean_cut | supported | supported |
| boolean_intersection | supported | supported |
| boolean_union | supported | supported |
| bounding_box | supported | supported |
| box | supported | supported |
| brep_serialization | supported | supported |
| center_of_mass | supported | supported |
| chamfer | supported | supported |
| cone | supported | supported |
| cylinder | supported | supported |
| extrude | supported | supported |
| fillet | supported | supported |
| loft | supported | supported |
| offset | supported | supported |
| primitive_creation | supported | supported |
| revolve | supported | supported |
| shell | supported | supported |
| sphere | supported | supported |
| step_export | supported | supported |
| step_import | supported | supported |
| surface_area | supported | supported |
| sweep | supported | supported |
| tessellation | supported | supported |
| topology_counts | supported | supported |
| usd_brep | unsupported | supported |
| validity | supported | supported |
| volume | supported | supported |

FreeCAD's Part tessellator accepts chord tolerance but not an angular tolerance; smlibpy receives both requested values. Triangle counts are reported as an approximate display comparison.

| Case | Match | Topology counts | OCCT volume (mm³) | SMLib volume (mm³) | Δ volume (mm³) | Max Δ bounds (mm) | Triangles OCCT / SMLib | Model/import time ms OCCT / SMLib |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| box | yes | same | 48000 | 48000 | 7.27596e-12 | 0 | 12 / 12 | 0.091 / 1.079 |
| box_with_cylindrical_through_hole | yes | different | 18571.7 | 18571.7 | 7.27596e-12 | 1.77636e-15 | 520 / 144 | 4.213 / 6.749 |
| cylinder | yes | different | 20357.5 | 20357.5 | 0 | 1.77636e-15 | 500 / 124 | 0.082 / 1.902 |
| filleted_rectangular_block | yes | different | 47598.7 | 47598.7 | 0 | 5.96856e-13 | 9452 / 1164 | 6.426 / 49.610 |
| multiple_boolean_bracket | yes | different | 27544.3 | 27544.3 | 2.18279e-11 | 0 | 2096 / 616 | 32.099 / 28.005 |
| revolved_spindle | yes | different | 37699.1 | 37699.1 | 0 | 3.55271e-15 | 1008 / 384 | 0.439 / 4.089 |
| swept_tube | yes | different | 502.655 | 502.655 | 2.27374e-13 | 7.10543e-15 | 500 / 124 | 0.633 / 3.171 |

STEP round-trip timings and deltas are recorded in the per-backend JSON files.
