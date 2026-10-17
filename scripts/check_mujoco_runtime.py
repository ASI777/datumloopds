#!/usr/bin/env python3
"""Execute an MJCF file emitted by the DatumLoop C++ exporter in MuJoCo."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import mujoco


def named_id(model: mujoco.MjModel, object_type: mujoco.mjtObj, name: str) -> int:
    result = mujoco.mj_name2id(model, object_type, name)
    if result < 0:
        raise AssertionError(f"missing MuJoCo object: {name}")
    return int(result)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mjcf", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    model = mujoco.MjModel.from_xml_path(str(args.mjcf))
    data = mujoco.MjData(model)
    if (model.nq, model.nv, model.nu) != (1, 1, 1):
        raise AssertionError(f"expected one slide coordinate, velocity and actuator; got {(model.nq, model.nv, model.nu)}")

    joint_id = named_id(model, mujoco.mjtObj.mjOBJ_JOINT, "slider_x")
    if model.jnt_type[joint_id] != mujoco.mjtJoint.mjJNT_SLIDE:
        raise AssertionError("exported one-axis joint is not a slide joint")
    if not bool(model.jnt_limited[joint_id]):
        raise AssertionError("exported slide joint does not have position limits")
    lower, upper = (float(value) for value in model.jnt_range[joint_id])
    if not (math.isclose(lower, -0.2) and math.isclose(upper, 0.2)):
        raise AssertionError(f"unexpected exported joint limits: {(lower, upper)}")

    body_id = named_id(model, mujoco.mjtObj.mjOBJ_BODY, "moving_body")
    mass = float(model.body_mass[body_id])
    inertia = [float(value) for value in model.body_inertia[body_id]]
    if not (math.isfinite(mass) and mass > 0.0 and all(math.isfinite(x) and x > 0.0 for x in inertia)):
        raise AssertionError(f"moving body has invalid mass/inertia: mass={mass}, inertia={inertia}")
    if any(inertia[i] + inertia[j] <= inertia[k] for i, j, k in ((0, 1, 2), (0, 2, 1), (1, 2, 0))):
        raise AssertionError(f"moving body principal inertias violate the rigid-body triangle inequality: {inertia}")

    actuator_id = named_id(model, mujoco.mjtObj.mjOBJ_ACTUATOR, "Ideal force actuator")
    if int(model.actuator_trnid[actuator_id, 0]) != joint_id:
        raise AssertionError("exported action does not target the expected slide joint")
    if not bool(model.actuator_ctrllimited[actuator_id]):
        raise AssertionError("exported force action is missing its control limit")
    ctrl_lower, ctrl_upper = (float(value) for value in model.actuator_ctrlrange[actuator_id])
    if not (math.isclose(ctrl_lower, -5.0) and math.isclose(ctrl_upper, 5.0)):
        raise AssertionError(f"unexpected actuator action bounds: {(ctrl_lower, ctrl_upper)}")

    position_sensor = named_id(model, mujoco.mjtObj.mjOBJ_SENSOR, "sensor.encoder.fixture.v1_position")
    velocity_sensor = named_id(model, mujoco.mjtObj.mjOBJ_SENSOR, "sensor.encoder.fixture.v1_velocity")
    if int(model.sensor_dim[position_sensor]) != 1 or int(model.sensor_dim[velocity_sensor]) != 1:
        raise AssertionError("encoder position/velocity sensors must each expose a scalar")

    qpos_address = int(model.jnt_qposadr[joint_id])
    dof_address = int(model.jnt_dofadr[joint_id])
    mujoco.mj_resetData(model, data)
    initial_position = float(data.qpos[qpos_address])
    data.ctrl[actuator_id] = 1.0  # N, inside the exported ±5 N action interval.
    for _ in range(10):
        mujoco.mj_step(model, data)
    # Re-evaluate position/velocity sensors at the final integrated state.
    mujoco.mj_forward(model, data)
    final_position = float(data.qpos[qpos_address])
    final_velocity = float(data.qvel[dof_address])
    if not (math.isfinite(final_position) and math.isfinite(final_velocity)):
        raise AssertionError("MuJoCo produced a non-finite joint state")
    if final_position <= initial_position or final_velocity <= 0.0:
        raise AssertionError("the exported actuator action did not move the slide joint in the expected direction")
    if not lower <= final_position <= upper:
        raise AssertionError("MuJoCo state escaped the exported joint limits")

    measured_position = float(data.sensordata[int(model.sensor_adr[position_sensor])])
    measured_velocity = float(data.sensordata[int(model.sensor_adr[velocity_sensor])])
    if not (math.isclose(measured_position, final_position, rel_tol=1e-9, abs_tol=1e-12) and
            math.isclose(measured_velocity, final_velocity, rel_tol=1e-9, abs_tol=1e-12)):
        raise AssertionError("MuJoCo encoder sensors did not update to the simulated joint state")

    report = {
        "backend": "MuJoCo",
        "version": mujoco.__version__,
        "mjcf": str(args.mjcf),
        "model_name": "One-axis <fixture>",
        "joint": "slider_x",
        "joint_range_m": [lower, upper],
        "body_mass_kg": mass,
        "body_principal_inertia_kg_m2": inertia,
        "actuator": "Ideal force actuator",
        "control_range_n": [ctrl_lower, ctrl_upper],
        "initial_position_m": initial_position,
        "final_position_m": final_position,
        "final_velocity_m_per_s": final_velocity,
        "encoder_position_m": measured_position,
        "encoder_velocity_m_per_s": measured_velocity,
        "steps": 10,
        "runtime": "passed",
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
