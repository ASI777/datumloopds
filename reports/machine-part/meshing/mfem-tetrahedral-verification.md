# MFEM tetrahedral adapter verification — 2026-10-18

This verifies a numerical adapter before applying machine-part loads. It is not
carrier FEA or mesh convergence evidence.

MFEM 4.10 consumes the checked explicit mesh as linear H1/P1 tetrahedra. Existing
H1/Q1 hexahedral verification remains available. The exact test is a 20 × 10 ×
8 mm body with 10 N uniaxial traction, E = 69.6 GPa, ν = 0.331. Only the normal
component on each minimum-coordinate face is fixed, permitting free Poisson
contraction. Thus the analytic solution is affine and P1 can represent it.

The machine-readable report is `mfem-tet-patch.json`. All nodal displacements and
all six stress/strain components are checked against the exact solution with
2e-7 relative/scale tolerance. Energy, equilibrium and the existing Q1 adapter
are independently compared. The configured solver may stop on its absolute
preconditioned residual tolerance before reaching the relative tolerance; the
actual reported residual is retained.

Schema 10 stores tetrahedral connectivity with field arrays, exact nominal CAD
SHA256 and backend build provenance. Versions 1–9 remain readable for their
previous layouts; tetrahedral fields mislabeled as version 9 are rejected.
The saved run is reopened, compared in full, and made available through the
native view model. Boundary display uses owner-cell centers for outward normals
so it does not assume a convex part or use the global center to orient holes.
No GPU or rendered-frame validation is claimed by these tests.

Tetrahedral parts require an explicit material stress probe. Uniform pressure
on curved faces is rejected until a varying-normal load integrator is qualified.
Resultant surface forces normalize by faceted area; body-force totals use actual
mesh volume. Small-strain, isotropic, static, CPU elasticity only. No contact,
plasticity, fastener preload, joint compliance or release-ready part verification.
