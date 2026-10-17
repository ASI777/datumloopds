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
  properties, initial state, target, random seed, and evidence-backed
  uncertainty metadata.

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
joint, transforms the axis origin through the sourced base-frame pose, uses the
explicit fixture mass and three principal inertias, and emits a motor with the
declared control bounds. It does not transfer CAD geometry, contacts,
multiple mechanism bodies, cameras, or electronics. The test checks the
generated XML text, but MuJoCo is unavailable in this environment, so no
engine parse or dynamics parity test has run. Backend-neutral specs remain the
DatumLoop contract; MuJoCo is only an export target at this stage.

Robot and RL environment records are not yet persisted in `.dld`, exposed in
the Qt UI, or dispatched as a solver job. No policy trainer, reward-learning
agent, RL rollout store, environment compiler, simulation batching, or digital
twin link is present.
