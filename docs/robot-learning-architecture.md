# Robot Learning Architecture

The robot-learning layer is independent of MuJoCo action and observation
types. The first native definitions are in
`src/datumloop/robotics/RobotLearning.*`:

- `RobotModel` selects a `DatumMechanism` and linear `DatumAxis` by stable ID,
  with a sourced base-frame pose in world coordinates. The pose uses a
  scalar-first normalized quaternion; the selected axis origin and direction
  remain defined in its base frame.
- `ActuatorDefinition` declares the axis, effort unit, sourced effort limit,
  and optional matching `DatumMotor` reference.
- `SensorDefinition` declares sensor type, update period, latency, source, and
  noise model. A linear encoder also declares sourced counts per metre. A
  camera placeholder declares image dimensions and field of view.
- `ObservationSpec` selects sensor IDs, and `ActionSpec` declares a bounded
  actuator command and unit.
- `RLEnvironmentSpec` carries the simulation timestep, episode limit, mass
  properties, initial state, sourced joint-position limits, target, random
  seed, and evidence-backed uncertainty metadata.

## One-axis environment

`DeterministicMassEnvironment` is a synthetic one-degree-of-freedom linear
slide. It integrates constant applied force on a point mass for fixed steps.
The command is force in newtons; the observation contains a quantized encoder
position and camera-placeholder metadata. The objective rewards lower
normalized position error and terminates within the declared position
tolerance; an episode also truncates at its step limit.

The fixture uses a 10 ms step, a 1 kg mass, a 0.1 m target, a 1 mm tolerance,
and a force command from -5 to +5 N. Its 10,000 count/m encoder updates every
20 ms with 10 ms latency. The camera placeholder has a 20 ms update period and
20 ms latency. These values are synthetic test-fixture settings, with explicit
evidence IDs; they are not machine specifications or measurements. No friction,
motor dynamics, joint reactions, collision, gravity, or state estimator is
included.

The camera observation always has `frameAvailable=false` and has no pixel
payload. The environment does not invent pixel noise. An encoder with no noise
model is an idealized, quantized sensor. Configured Gaussian noise requires a
positive, dimensionally compatible standard deviation and an evidence record;
the random seed is part of the environment specification.

## Domain randomization

The `UncertainParameter` contract stores a unit, distribution and parameters,
evidence, and optional correlation-group ID. Uniform bounds and Gaussian
standard deviation are validated. The default fixture contains no uncertain
parameters. These records are currently validation metadata: the environment
does not sample them or claim that any randomization interval describes a
machine population.

## MuJoCo export

`exportMuJoCoMjcf` maps the selected DatumLoop linear axis to an MJCF `slide`
joint, transforms the axis origin through the sourced base-frame pose, uses
explicit mass and principal inertias, emits sourced position limits, and
connects the motor control bounds. For each selected encoder it emits MuJoCo
joint-position and joint-velocity sensors. A camera placeholder remains
invalid and contributes no camera sensor or pixel data. CAD geometry,
contacts, multiple mechanism bodies, and electronics are not transferred.

The native robotics test writes the actual exporter output for the optional
runtime test. MuJoCo 3.16.0 loads and steps that MJCF, confirms its one slide
joint, travel range, mass/inertia, actuator target, state change, and updated
encoder values. The runtime record is
[`reports/validation/mujoco-runtime.json`](../reports/validation/mujoco-runtime.json).
This check does not establish parity with the in-house mass environment or
validate a real machine. Backend-neutral specs remain the DatumLoop contract.

## Structural ROM observation bridge

`addStructuralResponseObservation` combines a bounded 3D static ROM response
with a rigid-body `RobotObservation`. It records the ROM ID, applied-force
direction and magnitude, Young's modulus, predicted tip deflection, and
effective stiffness while preserving the rigid backend's existing sensors.
The held-out check uses the joint position and velocity returned by a verified
MuJoCo 3.16.0 runtime step, then passes all four MFEM-held-out force/modulus
pairs through the observation adapter. It compares structural deflection
against the MFEM run and direct ROM response, and records mean, RMS, maximum,
and worst-operating-point errors. The MuJoCo position and velocity stay
unchanged. MuJoCo continues to own rigid dynamics; this bridge adds the static
ROM result to the observation and does not alter forces or state integration.

This is observation-level coupling for a static, single-load-direction model.
The rigid MuJoCo state is held fixed across the comparison grid; the grid
measures ROM transfer through the observation adapter, not a MuJoCo structural
prediction. This is not flexible-body feedback, a transient structural solver,
or a complete MuJoCo environment adapter. Bridge evidence is recorded in
[`mfem-3d-elasticity.json`](../reports/validation/mfem-3d-elasticity.json).

Robot and RL environment records are not yet persisted in `.dld`, exposed in
the Qt UI, or dispatched as a solver job. No policy trainer, reward-learning
agent, RL rollout store, environment compiler, simulation batching, or digital
twin link is present.
