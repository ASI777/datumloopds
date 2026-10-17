# Numerical Verification

Numerical verification checks whether an implementation solves its stated
mathematical problem. It does not establish that the problem represents a real
machine. Material and load values in the fixtures below are synthetic and are
labelled in the test code.

## Registry and units

`EquationDefinition::validate` requires dimensioned terms on both sides of an
equation. Dimensions use integer exponents for the seven SI base dimensions.
Vector and tensor variables must declare a frame. Solver requests identify an
equation and design revision, state a minimum maturity, and provide named
inputs with units. The request is rejected for an unknown equation, insufficient
maturity, an input with a different dimension or unit symbol, or an invalid
transient time interval. Results carry solver identity, version, status,
dimensioned fields, and provenance.

The first steady/transient solver interfaces are contracts only. The equation
tests use closed-form checks directly and do not claim that a numerical PDE
backend has been integrated.

## Analytical cases

`datumloop-knowledge-tests` checks:

| Model | Fixture | Analytical result |
|---|---|---|
| Newton, constant force | `m=2 kg`, `F=6 N`, `t=3 s`, `x0=1 m`, `v0=-1 m/s` | `a=3 m/s^2`, `x=11.5 m` |
| Steady 1D heat conduction | `T(0)=300 K`, `T(L)=350 K`, `L=0.5 m`, `k=10 W/(m K)` | `T(0.2 m)=320 K`, `q=-1000 W/m^2` |
| Linear uniaxial elasticity | `P=1000 N`, `L=1 m`, `A=1e-4 m^2`, `E=200 GPa` | `delta=50 um`, `epsilon=5e-5` |

These are scalar checks for restricted cases of the registered formulations.
They do not test general rigid-body motion, transient heat flow, multiaxial
elasticity, constitutive nonlinearity, or material-property variation.

## Cantilever mesh refinement

The FEM fixture is a one-dimensional Timoshenko cantilever with two degrees of
freedom per node (transverse displacement and rotation), a fixed root, a free
tip, uniform transverse load, and a dense direct linear solve. The local
stiffness uses bending rigidity `EI` and shear stiffness `kGA`; shear terms use
two-point Gauss integration. The closed-form tip displacement is

```text
delta_tip = q L^4 / (8 E I) + q L^2 / (2 k G A)
```

The first term is the uniformly loaded cantilever bending solution; the second
is the shear contribution in the stated Timoshenko model. The classical
cantilever boundary conditions and bending solution are documented in the
[Jönköping University Applied Mechanics beam-theory notes](https://mechanics.ju.se/SolidMechanics/BeamTheory.html).
For the shear term, use `gamma(x)=V(x)/(kGA)` with `V(x)=q(L-x)` and integrate
the shear strain from root to tip, yielding `qL^2/(2kGA)`.

Synthetic fixture: `L=1 m`, `E=200 GPa`, `I=1e-5 m^4`, `kGA=2e7 N`, and
`q=1000 N/m`. The analytical value is `87.5 um`. Recorded results from
`datumloop-knowledge-tests`:

| Linear elements | Tip displacement (m) | Relative error |
|---:|---:|---:|
| 1 | 5.909090909e-5 | 32.47% |
| 2 | 7.672413793e-5 | 12.32% |
| 4 | 8.440594059e-5 | 3.536% |
| 8 | 8.669665810e-5 | 0.9181% |
| 16 | 8.729720960e-5 | 0.2318% |
| 32 | 8.744917873e-5 | 0.05808% |

The error decreases monotonically over this refinement sequence. The case
verifies one educational 1D element formulation; it is not a general solid
FEM solver, a mesh-quality study, stress-recovery check, or a production
structural capability.

## External numerical libraries

The adapter boundary should remain solver-neutral. The PETSc 3.26 documentation
identifies its current 2-clause BSD license; the project is relevant for
linear/nonlinear systems and time integration. MFEM 4.10 is the current
documented release, supports high-order and adaptive FEM, and carries a
BSD-3-Clause license. These licenses are suitable for further evaluation, but
there is no configured build, transitive dependency audit, or DatumLoop adapter
yet. Keep them as candidates until reproducible build, licensing, and
capability tests are complete.

- [PETSc release documentation](https://petsc.org/main/)
- [PETSc license](https://petsc.org/release/install/license/)
- [MFEM release and features](https://mfem.org/)
- [MFEM license](https://github.com/mfem/mfem/blob/master/LICENSE)

## Run

```sh
cmake --build build --target datumloop-knowledge-tests --parallel 1
./build/datumloop-knowledge-tests
```

The test prints the cantilever refinement results and held-out ROM error so
that the evidence remains reproducible with the source revision.
