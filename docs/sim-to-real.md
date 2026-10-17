# Simulation-to-Real Control Boundary

The first shared control boundary consists of `StateInterface`,
`CommandInterface`, `ControlState`, `ActuatorCommand`, and
`runControllerCycle` in `src/datumloop/robotics/RobotLearning.*`. A controller
receives a state snapshot with simulation time, axis identity, position,
velocity, and units, then returns an actuator ID, effort value, and unit.

`SimulatedRobotIo` implements both interfaces and checks the same command ID,
units, finite values, and limits as its configured simulated actuator.
`DisabledHardwareCommandInterface` implements the command interface but
always rejects writes. There is no real hardware driver or path that can issue
actuator commands in this slice.

Sensor observations retain capture and delivery timestamps, update periods,
latency, and validity. Encoder quantization and optional evidence-backed
Gaussian noise are explicit. The camera placeholder emits only its dimensions
and timing descriptor; it returns no image. Clock drift, jitter, packet loss,
transport protocols, camera pixels, IMU/force/current sensors, and clock-domain
synchronization are not implemented.

This contract is an initial software structure, not proof of sim-to-real
compatibility. There is no SIL, PIL, HIL, firmware bridge, safety supervisor,
telemetry capture, calibration loop, replay, policy evaluation, or physical
measurement. Any future hardware implementation must preserve these typed
state/command semantics, reject stale or invalid data, and add independent
safety and authorization gates before actuator authority.
