# Numerical Verification

Numerical verification checks whether an implementation solves its stated
mathematical problem. It does not establish that the problem represents a real
machine. Material and load values in the fixtures below are synthetic and are
labelled in the test code.

## Registry and units

`EquationDefinition::validate` requires dimensioned terms on both sides of an
equation. Dimensions cover the seven SI base dimensions and provide checked
addition, multiplication, division, and integer powers. Vector and tensor
variables require a reference frame and coordinate system. Equations may
record strong and weak forms and constitutive relationships. Solver requests
identify an equation and design revision, state a minimum maturity, and
provide named inputs with units. Requests reject unknown or under-mature
equations and incompatible input units. Solver results carry backend,
discretization, tolerances, convergence, benchmark, and input-revision
provenance.

The initial registry retains its three original reviewed equations and adds
`structural.timoshenko-beam.isotropic.v1` for the verified cantilever slice.
The Timoshenko record includes its coupled strong form, weak form, assumptions,
and source. Its convention uses shear strain `gamma = dw/dx - theta`; the
TU Delft open text documents this kinematics and its two-field finite-element
formulation ([source](https://teachbooks.tudelft.nl/computational-modelling/structural_linear/timoshenko.html)).

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

The DatumLoop reference fixture is a one-dimensional Timoshenko cantilever
with two fields (transverse displacement and cross-section rotation), a fixed
root, a free tip, and a uniform transverse load. The dense reference uses
linear elements. The analytical tip displacement is

```text
delta_tip = q L^4 / (8 E I) + q L^2 / (2 k G A)
```

The first term is bending deflection and the second is shear deflection for
the stated linear model. With `gamma(x)=V(x)/(kGA)` and
`V(x)=q(L-x)`, integrating the shear contribution from root to tip gives
`qL^2/(2kGA)`.

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

The reference error decreases monotonically. This narrow educational
formulation is not a general solid FEM solver, mesh-quality study,
stress-recovery check, or production structural capability.

## MFEM backend comparison

An optional MFEM 4.10 serial adapter assembles the same two-field weak form
using second-order H1 Lagrange spaces, integrates the bending and shear terms,
applies the root clamp, and solves the sparse system with conjugate gradients
and a symmetric Gauss-Seidel preconditioner. It records backend version, mesh,
polynomial order, tolerances, residual, strain energy, elapsed time, and
analytical comparison. The checked-in report is
[`reports/validation/mfem-cantilever-comparison.json`](../reports/validation/mfem-cantilever-comparison.json).

| MFEM line elements | Tip displacement (m) | Relative error vs analytical |
|---:|---:|---:|
| 1 | 8.452380952e-5 | 3.401% |
| 2 | 8.729166667e-5 | 0.2381% |
| 4 | 8.748657646e-5 | 0.01534% |
| 8 | 8.749915449e-5 | 0.0009663% |
| 16 | 8.749994705e-5 | 0.00006051% |
| 32 | 8.749999669e-5 | 0.000003784% |

At 32 elements, MFEM agrees with the analytical tip response to about
`3.8e-8` relative error and with the independent DatumLoop linear-element
reference to `5.81e-4` relative difference. This verifies this one-dimensional
benchmark only; it does not establish general structural capability. The
MFEM build was serial CPU-only, with MPI, METIS, CUDA, and HIP disabled. The
host had no usable NVIDIA driver, so GPU execution was not evaluated.
The registry marks only this restricted Timoshenko model E4 because the
analytical check and independent cross-backend comparison pass; no experiment
or machine-level validation is implied.

## External numerical libraries

The adapter boundary remains solver-neutral. MFEM is the finite-element
discretization and assembly library in this slice; its source uses a
BSD-3-Clause license. PETSc is a separate solver and time-integration toolkit
that can be evaluated for larger parallel linear/nonlinear systems and
preconditioning. PETSc is not installed or linked in this environment, and
the current small serial cantilever does not exercise capabilities that would
justify its extra build and MPI integration. Revisit PETSc with a larger
parallel benchmark before adopting it.

- [PETSc release documentation](https://petsc.org/main/)
- [PETSc license](https://petsc.org/release/install/license/)
- [MFEM release and features](https://mfem.org/)
- [MFEM license](https://github.com/mfem/mfem/blob/master/LICENSE)

No PETSc solve or performance comparison is claimed.

## Run

```sh
cmake -S . -B build/native -DBUILD_TESTING=ON \
  -DMFEM_DIR=/path/to/mfem-config-dir
cmake --build build/native --parallel 2
ctest --test-dir build/native --output-on-failure
```

MFEM is optional. When found, CTest adds `datumloop-mfem-tests`; the native
suite also retains the reference FEM, equation, and held-out ROM checks.
