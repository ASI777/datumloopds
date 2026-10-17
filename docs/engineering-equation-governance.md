# Engineering Equation Governance

DatumLoop treats an equation as versioned engineering data, not prose generated
by an agent. The first native registry is implemented in
`src/datumloop/knowledge/EquationRegistry.*`. It is intentionally small; it is
not a general computer algebra system or a production physics catalogue.

## Evidence path and promotion

The required review path is:

```text
source → formulation → dimensional check → symbolic/invariant review
       → numerical verification → benchmark → experiment → versioned approval
```

The registry stores the equation ID and name, domain, symbolic form, typed
variables, SI dimensions, vector/tensor frame metadata, dimensioned terms,
assumptions, validity range, boundary and initial conditions, literature
sources, verification records, and maturity. A passed verification must point
to evidence. IDs are versioned (`...v1`); changing assumptions or formulation
requires a new ID so old run manifests can remain reproducible.

Maturity gates are:

| Level | Evidence required |
|---|---|
| E0 | Unreviewed hypothesis |
| E1 | Traceable literature source |
| E2 | Dimensional and symbolic checks passed |
| E3 | At least one analytical benchmark passed |
| E4 | Independent numerical implementation agrees |
| E5 | Published experiment validated |
| E6 | DatumLoop hardware measurements validated |

Callers state a minimum maturity. A solver request is rejected if its model is
below that level. The current native solver contract defaults to E3. Promotion
is never automatic: adding a source or passing one test cannot promote an
equation past the evidence that test covers.

## Current registry

The initial registry contains three equations:

| ID | Form | Current maturity | Implemented check |
|---|---|---:|---|
| `mechanics.newton.second-law.translation.v1` | `sum(F) = m a` | E3 | Constant-force 1D trajectory |
| `thermal.heat-equation.isotropic.v1` | `rho cp dT/dt = div(k grad(T)) + Q` | E3 | Source-free 1D steady slab limit |
| `materials.elasticity.uniaxial-linear.v1` | `sigma = E epsilon` | E3 | Axial rod extension and strain |

Each equation records a MIT OpenCourseWare source in the registry. The tests
check dimensions and the stated analytical example. They do not provide an
independent numerical backend, experimental correlation, or hardware
validation, so E4-E6 are unavailable for these entries.

The symbolic checks recorded today are explicit review statements checked for
presence by the registry. There is no symbolic algebra engine, automatic
derivative system, tensor contraction checker, or independent verification
agent in this slice. Likewise, frame names are required on vector/tensor
variables, but automatic frame transformations are not implemented.

## Missing values and source types

Unknown physical constants stay unknown. A model request must report the
missing quantity or obtain it from a cited reference, a manufacturer source, a
material database, measurement, or a separately reviewed identification
result. A fixture value may be used for a synthetic verification case only
when labelled as such; it is not copied into a real machine model.

Keep published, measured, derived, calibrated, assumed, and synthetic-fixture
values distinguishable. Each property should carry its unit, evidence kind,
source identity, source revision or measurement record, and validity range.
Agents can propose equation definitions and tests, but a different review
step must evaluate the evidence before an equation is approved for higher
maturity.

## Scope boundary

This registry is not yet persisted in `.dld`, connected to the document's
materials, or used to execute a physics solver. Generic steady and transient
solver interfaces exist, with request units, revision IDs, tolerances, and
result provenance contracts. No structural, thermal, fluid, electromagnetic,
chemical, metallurgical, or multiphysics backend is wired to them. See
[`numerical-verification.md`](numerical-verification.md) for the analytical
and FEM checks and [`sim-to-real.md`](sim-to-real.md) for hardware boundaries.
