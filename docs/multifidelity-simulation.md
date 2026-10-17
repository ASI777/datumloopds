# Multifidelity Simulation

The first fidelity demonstration connects a serial MFEM response to a faster
reduced response:

```text
uniformly loaded cantilever
        ↓
32-element quadratic H1 MFEM Timoshenko response
        ↓
fit one scalar compliance from an 8-element MFEM model
        ↓
predict tip displacement in a cheap load-response model
```

Here “high fidelity” means only the higher-order, refined 1D MFEM response for
this benchmark. It is not a 3D twin or a validated industrial structural
model. A separate dense linear-element DatumLoop implementation provides a
cross-backend reference on the same cantilever problem.

## Reduced response

Training loads are `250`, `500`, and `1000 N/m`. The fit stores one compliance
coefficient, with displacement predicted linearly from distributed load.
Training loads are `250`, `500`, and `1000 N/m` at 8 elements. Validation uses
held-out loads `350` and `750 N/m` against the 32-element MFEM response. The
maximum and RMS relative held-out errors are each about **0.000963%**. The full
mesh, energy, residual, runtime, solver identity, and held-out data are in
[`mfem-cantilever-comparison.json`](../reports/validation/mfem-cantilever-comparison.json).

The model records the source solver and version, source-model revision,
cantilever length, modulus, second moment and shear stiffness, training and
held-out meshes, sample loads, validity range, and max/RMS relative errors.
Predictions outside the training-load interval are rejected.
This is still a single linear compliance response: it has no state, memory,
frequency response, contact, plasticity, thermal coupling, or parameter
uncertainty. No automatic fidelity selection is implemented.

## Backend scope

MFEM is integrated as an optional serial H1 adapter for the one-dimensional
cantilever benchmark. The existing sparse solver uses conjugate gradients with
a symmetric Gauss-Seidel preconditioner. The build has MPI, METIS, CUDA, and
HIP disabled; no GPU run is claimed.

PETSc remains under evaluation, not integration. It provides a separate
linear/nonlinear solver and time-integration layer that may be useful for
larger parallel problems and preconditioning. It is not installed in the
checked environment, and this small serial cantilever does not establish a
need for it. Evaluate PETSc with a larger parallel workload before adding the
dependency. Library release and license references are in
[`numerical-verification.md`](numerical-verification.md).

MuJoCo is the first fast robotics-backend export target. Release `3.16.0` was
published on 2026-10-14 under the project's [Apache-2.0 license](https://github.com/google-deepmind/mujoco/blob/main/LICENSE).
The exporter emits MJCF for one linear slide joint, its sourced travel limits,
an ideal motor with declared action bounds, explicit mass and inertia, and
position/velocity sensors for the encoder. It applies the sourced base-frame
pose and axis origin to the body transform; the camera placeholder emits no
image sensor or pixels. The actual C++-exported model was loaded and stepped by
MuJoCo 3.16.0. A 1 N action over ten 10 ms steps moved the synthetic 1 kg mass
to `0.0055 m` at `0.1 m/s`, within the `[-0.2, 0.2] m` joint range, and the
encoder sensors matched the final state. See
[`mujoco-runtime.json`](../reports/validation/mujoco-runtime.json). This is
runtime parsing and a small dynamics check, not parity with the in-house
environment or a real machine.
