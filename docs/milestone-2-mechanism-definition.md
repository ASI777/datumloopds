# Milestone 2: mechanism definition

**Status: in progress.** This change adds the first versioned mechanism-data
contract and native project persistence. It is a foundation for mechanism
authoring; it does not complete the Milestone 2 gate.

## Implemented

`MechanismSpec` version 1 is stored in `DatumEngineeringModel`. Its bodies have
stable mechanism IDs mapped one-to-one to CAD object IDs and a geometry-source
descriptor for embedded B-Rep, the native feature graph, a FreeCAD feature, an
SMLibPy script, or a STEP import. The model stores optional mass, local center
of mass, and inertia tensor values in SI units. Each supplied mass property
must have a provenance record; missing values remain unknown.

The spec also stores body-local joint frames, represented by metre-valued
origins and normalized `[w, x, y, z]` quaternions; fixed, revolute, prismatic,
spherical, and universal joints; position, velocity, and effort actuators
with sampled trajectories and optional limits; contact pairs with stable
surface references and friction/contact parameters; and requirements with
units, relations, bounds or targets, tolerances, and a hard-constraint flag.
Existing datum axes and motors remain supported for compatibility.

Core validation checks stable-ID uniqueness, body-to-object mapping, source
locator requirements, finite/ranged values, unit quaternions, positive
semidefinite inertia tensors, entity references, and requirement bounds.
Document integration verifies CAD object ownership, native feature-graph
source references, and contact face ownership. Study-readiness diagnostics
check for missing or disconnected body/joint inputs, mass properties for
dynamics, assigned material definitions with sourced elastic modulus and
Poisson ratio for structural inputs, and resolvable contact surfaces and
friction data for contact inputs.

The `.dld` project format is now schema 6. It writes all version-one mechanism
fields and continues to read schemas 1 through 5. Older mechanism records are
preserved as legacy CAD-object references, axes, and motors; they are not
silently upgraded into authored body or joint definitions. The full field
contract and remaining persistence limitations are in
[`native-project-format.md`](native-project-format.md).

## Verification

The native tests cover mechanism input validation, study-readiness diagnostics
for kinematics/dynamics/structural/contact inputs, complete schema-six
round-trip, and schema-five migration. The full native build and CTest suite
pass with 10 of 10 test targets.

These checks establish data validation and persistence behavior only. A
`readyForStudy()` result means required input records passed these checks; it
does not mean that a supported solver exists, constraints are mutually
consistent, or any numerical analysis has run or passed verification.

## Remaining work for the Milestone 2 gate

- Add UI and transactional commands to author, inspect, and repair mechanism
  bodies, frames, joints, actuators, contacts, materials, and requirements.
- Implement native FreeCAD-feature and STEP import paths, and an isolated,
  revision-pinned SMLibPy script path; geometry-source enum values alone do not
  implement these adapters.
- Calculate mass properties from exact geometry and declared density, record
  conversion checks, and preserve the input source and evidence.
- Validate joint-frame compatibility, closed-chain and constraint consistency,
  and ambiguous or stale geometry references after regeneration or transfer.
- Persist and validate checkpoints plus the versioned study and run records
  required to reproduce downstream solver work.

The M2 gate remains: create and reopen a complete mechanism with reproducible
geometry and physical metadata. Until the authoring and geometry workflows
exist, this gate is not met.
