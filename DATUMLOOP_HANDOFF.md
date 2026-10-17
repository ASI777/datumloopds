# DatumLoop checkpoint — user requested pause, 2026-10-18

## Repository and review state

Canonical remote: https://github.com/ASI777/datumloopds.git.
Active worktree: `/tmp/datumloopds-machine-part`, branch `codex/machine-part-fea`.
Use this checkout, not the old untracked/unborn snapshot at the original workspace
root `/home/atedge/Documents/ChatGPT/datumloopds`.

- PR1: https://github.com/ASI777/datumloopds/pull/1 — frozen CAE/ROM/MuJoCo,
  ready for review, not merged; head `00dda89875380c111548ef069012591e78b8f690`.
- PR2: https://github.com/ASI777/datumloopds/pull/2 — frozen sourced native
  machine-part CAD/manufacturing; ready for review; head
  `079ae6501ba241ff3ed935b8320a1c08f272a9e5`.
- PR3: https://github.com/ASI777/datumloopds/pull/3 — separate **draft** FEA work,
  base `codex/machine-part-design`, head `codex/machine-part-fea`.
- Verified mesh commit `c480aad`; verified MFEM/P1/persistence commit `d2c7769`.
- The subsequent pause checkpoint intentionally includes work in progress.
  Do not interpret a saved commit as completed acceptance or fully tested code.

## Executed engineering chain

Official Maxon 2025 catalog page 259, standard flange, 18 V reference application:
https://www.maxongroup.com/medias/sys_master/root/9399287709726/Cataloge-Page-EN-259.pdf
PDF SHA256: 9aa3139520c7f4c6cdd5c74c1dafe24df351556142f77a1e26500e9c2b405d17.
Original PDF was visually inspected; no motor dimensions were inferred from pixels.
The user delegated choosing the specific application and optimizing it.

The part is a 34 x 44 x 1.5 mm 6061-T6 Z-spindle motor carrier, eight real bores,
nine editable native features, exact OCCT shape and fixed motor/gantry interfaces.
Only thickness may change, bounded 1.5–5.5 mm. Source-only unknowns, explicit
application selections and their provenance are retained separately.

A pinned isolated Gmsh 4.15.2 process generates tetrahedra. DatumLoop independently
checks source geometry digest, project/revision, all 14 persistent CAD face groups,
trimmed-face boundary-node positions, solid membership, positive cell quality,
complete exterior boundary association and CAD/mesh area/volume differences.
Do not replace this with a custom general tetrahedral mesher.

MFEM H1/P1 tetrahedral small-strain elasticity passed an exact affine uniaxial
patch test: 1,096 tets, 329 nodes, maximum nodal error 7.24e-17 m, expected strain
energy 1.7959770114942526e-7 J. Existing hex/Q1, ROM, MuJoCo, native field display
and hardware-write rejection remain intact. Schema 10 stores tetrahedral fields;
older schemas remain readable. Native view-model availability and field display
were tested, not hardware GPU rendering.

## Actual carrier FEA (not AI estimates)

The selected static envelope combines motor/accessory mass 40 g, acceleration
60 m/s², gravity 9.80665 m/s², motor normal force 2.792266 N, reaction torque
0.1935 N*m and carrier self inertia using sourced density 2710 kg/m³.
Two M4 bore surfaces are ideal fixed restraints. Three motor bore tractions
reproduce the selected force/torque. No contact, preload or joint compliance is
modeled. Belt radial force is excluded because its required inputs are unknown.

| Requested size mm | Tetrahedra | Motor-interface displacement µm | Angular deflection mrad |
| --- | ---: | ---: | ---: |
| 1.20 | 48,119 | 17.7372 | 0.745120 |
| 0.80 | 66,536 | 20.8394 | 0.885832 |
| 0.50 | 130,906 | 23.9303 | 1.02224 |
| 0.35 | 251,845 | 25.6531 | 1.09454 |
| 0.25 | 605,078 | 27.3748 | 1.16792 |

Limits are 5 µm and 0.2 mrad: **FAIL**. Latest run: 131,963 nodes, 395,889 DOF,
3,331 iterations, strain energy 2.7387974011830026e-5 J, reaction Z
-3.1924809722502796 N, relative equilibrium error 5.721479410032108e-11,
maximum von Mises stress 12.562998 MPa. The 100 MPa limit is a selected screening
cap, not a sourced yield stress or a certified safety factor.

Latest 0.35 → 0.25 changes: displacement 6.2893%, angle 6.2834%, energy 5.8675%,
point stress 31.2776%. The current 5% convergence gate is **NOT met**. Do not
loosen it or hide the point-stress variation just to claim success.
The 0.25 mm solve/field stage took 256.617 s; worker stage 73.139 s. Its recorded
worker memory (~790 MB) was measured before result serialization in that executed
version; the later checkpoint moves measurement to completion but is not yet
fully regression-tested. No GPU acceleration was executed or claimed.

## Current code / what remains unverified

`CarrierStructuralDesign` builds this real load case, extracts maximum motor
interface motion, fits small angular motion from area-weighted bolt-face motions,
and proposes a bounded thickness change with an engineering reason. The inverse
cube bending rule is a hypothesis, never a substitute for MFEM. Unit tests checked
load equivalence, synthetic rigid-motion metric extraction, bounded changes,
fixed interface preservation and native CAD regeneration. Synthetic test fields
are not simulation results.

`datumloop-carrier-analysis --redesign` implements a controller capped at four
candidates. It requires convergence before accepting a critic step. **No complete
automatic regenerate/remesh/re-solve improvement was executed or verified yet.**
Preserve failed candidates and all metrics. Never claim this milestone complete.

`analysis-study.json` revision v3 now explicitly retains four critical UNKNOWNs:
belt pretension/radial force, pulley overhang, mounting-joint compliance/preload,
and applicable material yield strength. It permits the defined conditional load
case to be computed, but overall requirements acceptance remains false. The
executed 0.25 mm snapshot used v2 (three unknowns); its exact input remains in
its saved candidate directory. Current source v3 adds yield as an explicit gap.

Latest fully completed native regression: **16/16 passed** (MFEM 4.10, MuJoCo
3.16 and Gmsh enabled), before the final small pause-checkpoint edits. Native
independence passed after the tetrahedral adapter changes. The final worker
completion-memory record, extra yield-unknown assertion, renamed effective force/
motion ratio, force-balance gate, failure report and report metadata edits still
need rebuilding and regression verification. Do that first; do not claim that
these last edits already passed the full suite.

## Runtime, outputs and durable backup

Build SDK: `/tmp/datumloopds-mfem-4.10-build`, source sibling
`/tmp/datumloopds-mfem-4.10-src`. Gmsh interpreter
`/tmp/datumloopds-gmsh-venv/bin/python`; MuJoCo interpreter
`/tmp/datumloopds-mujoco-venv/bin/python`. CMake cache already selects these.

- `build/carrier-study/initial-coarse/`: actual coarse source, fields and reports.
- `build/carrier-study/initial-convergence/`: preserved partial 1.2/0.8 runs;
  0.5 worker failed with disk quota.
- `build/carrier-study/initial-fine/`: preserved partial 0.5/0.35 runs;
  0.25 worker failed under the earlier process budget with an empty diagnostic.
- `build/carrier-study/initial-025/`: successful 605,078-cell CPU run, exact input,
  full `result.dld` (~625 MB), source and metrics.
- Committed small reports and actual-field image: `reports/machine-part/fea/`.

The user-requested durable checkpoint is under
`/home/atedge/Documents/ChatGPT/datumloopds/saved-work/2026-10-18/`:
Git bundle, complete carrier study outputs, optional dependency runtime archive,
original source PDF and test logs. Restore the Git bundle to recover source if
`/tmp` is cleared. Runtime archives preserve original paths; inspect before
restoring. The bundle is authoritative for code, not the old root snapshot.

Only rebuildable outputs from an earlier FreeCAD build were cleaned to recover
~1.5 GB quota. Source, installed applications, Git stashes and engineering
evidence were retained. Do not spend the next slice repairing FreeCAD.

## Next work for Argon 4

1. Read this file and the user's milestone instructions. Verify branch, commits,
   remote, PR state, clean checkpoint and saved data. Resume the goal only when
   the user asks; it is intentionally paused now.
2. Build and run all native tests with MFEM, MuJoCo and pinned Gmsh, plus native
   independence. Fix any checkpoint regressions before new functionality.
3. Address numerical convergence on this **same real carrier**. Consider justified
   refinement/error assessment and investigate P1 thin-plate/point-stress behavior.
   Preserve all previous failures and the unchanged acceptance limits. Measure
   memory and runtime; large JSON snapshots and Gmsh resource budgets matter.
   Do not silently substitute AI calculations, a rectangular demo, or relaxed
   acceptance. A 0.19 mm mesh is only a possible next experiment, not a proven fix.
4. Once the relevant CPU analysis is demonstrably converged, execute the controlled
   thickness critic loop. Regenerate exact editable CAD, independently remesh,
   re-solve and quantitatively demonstrate improvement over the failed 1.5 mm
   candidate. Prove fixed mating dimensions and manufacturing rules survive.
5. Keep conditional static success distinct from full machine qualification.
   Resolve/explicitly select missing application inputs using reliable evidence;
   never invent motor dimensions, yield strength, belt forces or joint behavior.
6. Only after verified local CPU results: portable AnalysisJob/SolverResult,
   provider-neutral IComputeBackend, actual optional Colab MFEM GPU execution,
   CPU/GPU numerical comparison and measured performance. Do not confuse a GPU
   being detected with MFEM actually executing its supported GPU path.
7. Preserve existing ROM/MuJoCo. A future real-part ROM needs new geometry/material/
   mesh/solver/domain identity; do not reuse the rectangular ROM or train RL now.
8. Keep PR1/PR2 frozen and PR3 draft until evidence supports review. Report hashes,
   tests, quantitative results, limitations and the next blocker per slice.
