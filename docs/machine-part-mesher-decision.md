# Machine-part volume mesher decision — 2026-10-17

Status: optional external worker implemented and executed on the carrier.
The dependency artifact is pinned in `requirements-meshing.lock`. This slice
verifies volume meshing and CAD association; real-part MFEM FEA is not yet run.

Gmsh 4.15.2 is selected for the first optional external worker prototype.
DatumLoop will own mesh requests, units, CAD revision, persistent-face mapping,
mesh quality gates and result association. The worker will generate the mesh.
The existing rectangular hex mesher and its MFEM verification remain available.

| Check | Gmsh evidence and decision |
| --- | --- |
| License | GPL-2.0-or-later with its stated combination exception; preserve notices. DatumLoop currently declares GPL-3.0-or-later. No repository license changes are made. |
| Exact geometry | Official OCC API imports BREP, STEP and IGES; use the generated exact BRep, with explicit millimetre-to-SI conversion. |
| Elements | Unstructured 3D tetrahedra; structured/extruded paths also support hex/prism/pyramid. The first carrier path will target tetrahedra, with no general hex guarantee. |
| Boundary groups | Physical groups and integer entity tags are represented in the documented mesh/API formats. DatumLoop face UUIDs must be mapped uniquely before meshing and every required boundary verified after export. |
| Linux | Official Linux binaries/distribution packages and multi-language API documented. Isolated Linux worker executed with OCC 7.8.1; build information is recorded in the result. |
| Integration | Separate optional process to isolate meshing runtime/failures and preserve a portable request/response boundary. Pin the artifact version and hash before adding it to a build profile. |

Sources: [Gmsh 4.15.2 manual](https://gmsh.info/doc/texinfo/),
[license](https://gmsh.info/doc/texinfo/#License),
[OCC import API](https://gmsh.info/doc/texinfo/#gmsh_002fmodel_002focc_002fimportShapes),
[mesh algorithms](https://gmsh.info/doc/texinfo/#Mesh-module), and
[physical groups](https://gmsh.info/doc/texinfo/#Elementary-entities-vs-physical-groups).

Netgen is a viable alternative: its upstream license is LGPL-2.1-or-later and
its documented OCC build supports STEP geometry. Its boundary-label transfer
and hex path have not been qualified for this workflow; it is not selected for
the first prototype. Sources: [upstream license](https://github.com/NGSolve/netgen/blob/master/LICENSE)
and [OCC geometry documentation](https://docu.ngsolve.org/latest/i-tutorials/unit-4.4-occ/occ.html).

Before a part solve, implementation must demonstrate unique CAD-face matching,
complete boundary coverage, SI coordinates, positive volume/quality, bounded
mesh resources, backend/version provenance and rejected ambiguous mappings.
The solver must consume the returned mesh; generating a mesh alone does not
verify MFEM boundary conditions. CPU convergence and redesign evidence precede
any portable remote/GPU execution work.


## Executed qualification — 2026-10-18

`IAnalysisMesher` now accepts a project/revision/object-scoped SI mesh request.
The Gmsh process receives exact nominal BRep, uniquely matches each persistent
face using surface type/area/centroid/bounds, and assigns physical groups. The
native reader independently checks every boundary node against its trimmed CAD
face, every node against the solid, positive elements, exterior-face coverage,
quality, CAD/mesh volume and surface areas. Ambiguous matches fail.

The carrier generated 48,119 linear tetrahedra with 12,938 nodes, 18,838 boundary
triangles and all 14 face groups. The measured values and exact source identity
are in `reports/machine-part/meshing/carrier-mesh.json`. Tests reject inverted
cells, missing exterior triangles, swapped equal-area hole face mappings,
stale requests and an insufficient element cap. Display tessellation does not
change the nominal geometry digest.

Limits: one solid; uniquely identifiable plane/cylinder faces; linear tetrahedra;
no general hexahedral meshing claim. Time, address-space and output limits bound
the worker; element counts are checked before and after generation. Boundary
faceting is approximate and must meet explicit area/volume gates. The solver
still needs tetrahedral consumption and numerical patch verification before a
machine-part solve or redesign can be claimed. This is not mesh convergence.
