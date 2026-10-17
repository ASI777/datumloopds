# CAE / ROM review evidence — 2026-10-17

This slice is frozen for review on `codex/cad-driven-3d-elasticity`.
The authoritative repository is ASI777/datumloopds, integration branch `main`;
its clean baseline was checked at `2b8f995989de850fcdc7dcda18cf12d329f0651d`.
Machine-part requirements, arbitrary-part meshing, automatic redesign and remote
GPU execution belong on a separate branch after this slice.

## Executed verification

- Release native build with MFEM 4.10.0 and MuJoCo 3.16.0 enabled: passed.
- Full CTest suite: 11/11 passed, including actual exported-MJCF integration
  before the MFEM/MuJoCo observation test.
- Native independence check and Python syntax check: passed.
- Four held-out load/modulus samples: MuJoCo+ROM versus MFEM mean absolute
  relative error 6.68897352787e-10, RMS
  7.89159569407e-10, maximum 1.08778377474e-09.
- Ten MuJoCo steps from 0.01 to 0.10 s:
  explicit structural load 5 to 15 N; quasi-static deflection
  220.859939 to 662.579816 µm.
  Rigid sensor values were preserved. Duplicate times and out-of-domain loads
  were rejected; rejected loads did not advance adapter time; reset passed.

Machine-readable evidence: `mfem-3d-elasticity.json` and `mujoco-runtime.json`.
The configured runtime trace is read from build output. A build without MuJoCo
reports its runtime test skipped and does not treat a committed historical trace
as live time-series evidence.

## Limits for review

- Meshing, solver transfer and field display support one axis-aligned rectangular
  OCCT solid and Q1 hexahedra. No practical machine-part mesher is implemented.
- Static homogeneous isotropic small-strain elasticity only; no contact,
  plasticity, flexible dynamics or experimentally established accuracy.
- ROM validation varies load and modulus on one geometry and mesh. It does not
  establish transfer to another geometry. The ROM is not persisted in `.dld`.
- The per-step structural load is an explicit fixture schedule independent of the
  MuJoCo actuator. Coupling adds observations without force feedback.
- NVIDIA viewport harness is implemented, but hardware rendering is unverified
  because this host has no usable NVIDIA driver/device. GPU FEM is unimplemented.
- The synthetic viewport overlay fixture is display-only. Engineering fields
  in the numerical report come from MFEM.
- FreeCAD remains compatibility-only. Physical hardware writes remain rejected.
