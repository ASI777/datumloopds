# Multifidelity Simulation

The first fidelity demonstration connects a small FEM response to a faster
reduced model:

```text
uniformly loaded cantilever
        ↓
32-element Timoshenko FEM response
        ↓
fit one scalar compliance from an 8-element FEM model
        ↓
predict tip displacement in a cheap load-response model
```

The high-fidelity reference in this demonstration means only the 32-element
mesh in the same simple 1D formulation. It is not a high-fidelity 3D twin or a
validated industrial structural model.

## Reduced response

Training loads are `250`, `500`, and `1000 N/m`. The fit stores one compliance
coefficient, with displacement predicted linearly from distributed load.
Validation uses held-out loads `350` and `750 N/m` against the 32-element
reference. The maximum relative held-out error is **0.8605%** for the fixture
documented in [`numerical-verification.md`](numerical-verification.md).

The model is valid only for the same linear beam assumptions and property
values. It has no state, memory, frequency response, contact, plasticity,
thermal coupling, parameter uncertainty, or extrapolation guard. A real
reduced model must record its training design, training points, basis or
coefficient data, independent validation cases, error metric, validity range,
source model revision, and provenance. DatumLoop does not yet persist those
artifacts or select a fidelity automatically.

## Candidate backend evaluation

PETSc is a possible linear/nonlinear and transient infrastructure layer; MFEM
is a possible high-order/adaptive finite-element layer. The official PETSc
license is 2-clause BSD, and MFEM's source license is BSD-3-Clause. Current
release and evaluation details are recorded in
[`numerical-verification.md`](numerical-verification.md). Neither library is
integrated in this slice.

MuJoCo is the first fast robotics-backend export target. Release `3.16.0` was
published on 2026-10-14 under the project's [Apache-2.0 license](https://github.com/google-deepmind/mujoco/blob/main/LICENSE).
The initial exporter emits MJCF for one linear slide joint and an ideal motor
with the declared action bounds. It applies the sourced base-frame pose and
axis origin when writing the MJCF body transform. The [official XML reference](https://mujoco.readthedocs.io/en/latest/XMLreference.html)
defines the slide-joint and actuator model. The test checks XML well-formedness
and the selected joint/action fields. MuJoCo is not installed in this environment, so model compilation,
step comparison, performance, and parity with the in-house fixture have not
been verified. This is an export prototype, not a backend adapter or a
cross-simulator validation.
