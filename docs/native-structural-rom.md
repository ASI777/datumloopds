# Bounded 3D structural response ROM

The first DatumLoop 3D reduced model is a bounded static response surface fit
from real MFEM runs on the CAD-derived 200 x 10 x 10 mm OCCT cantilever. Every
sample uses the same 1,280-element, 2.5 mm structured hex mesh, the same
persistent clamp/load faces, the same equation and solver settings, and the
same sourced material definition. Its Young's modulus and applied force vary;
each high-fidelity run ID and material revision is retained in the report.

The model fits the linear-elastic scaling law

```text
tip displacement = normalized compliance [1/m] * applied force [N]
                   / Young's modulus [Pa]
effective stiffness = applied force / predicted tip displacement
```

The training grid contains nine MFEM runs at loads of 5, 10, and 15 N and
Young's modulus factors of 0.973, 1.000, and 1.027 relative to the sourced
69.6 GPa mean. The ±2.7% range follows the uncertainty recorded in the cited
NBS compilation. Four separate MFEM runs at 7.5 and 12.5 N and modulus
factors of 0.9865 and 1.0135 provide held-out parameter validation.

The validity region is explicit: 5–15 N and 67.7208–71.4792 GPa. Predictions
outside either interval throw an out-of-range error; the model does not
extrapolate. On the four held-out samples, maximum relative error was
`1.09e-9` and RMS relative error was `7.89e-10`, below the 1% acceptance limit.
The full training and validation values, run IDs, mesh revision, and material
revisions are in
[`mfem-3d-elasticity.json`](../reports/validation/mfem-3d-elasticity.json).

This is a first response ROM for static tip displacement and effective
stiffness. The held-out points vary load and material modulus but share the
same CAD geometry, mesh, governing equation, and MFEM backend as training. The
error therefore measures parameter-response fit, not mesh transfer,
experimental validation, or model discrepancy. The ROM is not yet stored in
`.dld` or consumed by MuJoCo; dynamic modes, flexible-body coupling, and
fast-versus-high-fidelity environment comparison remain future work.
