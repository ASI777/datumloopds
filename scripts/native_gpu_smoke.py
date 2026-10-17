#!/usr/bin/env python3
"""Run and report the native viewport smoke test on an NVIDIA-backed X11 display."""

from __future__ import annotations

import argparse
import csv
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
POINTER_SMOKE = ROOT / "scripts" / "native_pointer_smoke.py"


def run(command: list[str], *, timeout: int = 20) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=timeout, check=False)


def gpu_devices() -> tuple[list[dict[str, str]], str | None]:
    nvidia_smi = shutil.which("nvidia-smi")
    if nvidia_smi is None:
        return [], "nvidia-smi is not installed"
    result = run([
        nvidia_smi,
        "--query-gpu=name,driver_version,uuid",
        "--format=csv,noheader",
    ])
    if result.returncode != 0:
        return [], (result.stderr or result.stdout).strip() or "nvidia-smi could not query the NVIDIA driver"
    devices = []
    for row in csv.reader(result.stdout.splitlines(), skipinitialspace=True):
        if len(row) >= 3:
            devices.append({"name": row[0].strip(), "driver_version": row[1].strip(), "uuid": row[2].strip()})
    return devices, None if devices else "nvidia-smi returned no NVIDIA devices"


def screen_resolution() -> str | None:
    xrandr = shutil.which("xrandr")
    if xrandr is None:
        return None
    result = run([xrandr, "--current"])
    match = re.search(r"current\s+(\d+)\s+x\s+(\d+)", result.stdout)
    return f"{match.group(1)}x{match.group(2)}" if match else None


def parse_runtime_values(output: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in output.splitlines():
        match = re.search(r"DATUMLOOP_([A-Z_]+)=(.*)$", line)
        if match:
            values[match.group(1)] = match.group(2).strip()
    return values


def write_report(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--report",
        type=Path,
        default=ROOT / "reports" / "validation" / "native-gpu-smoke.json",
        help="JSON report path (default: reports/validation/native-gpu-smoke.json)",
    )
    parser.add_argument(
        "--allow-missing-gpu",
        action="store_true",
        help="write a skipped report and exit successfully when no NVIDIA driver/device is available",
    )
    args = parser.parse_args()
    report_path = args.report if args.report.is_absolute() else ROOT / args.report
    capture_dir = report_path.parent / "native-gpu-captures"
    devices, gpu_error = gpu_devices()
    report: dict[str, Any] = {
        "schema_version": 1,
        "status": "failed",
        "commit": None,
        "operating_system": platform.platform(),
        "display": {"server": os.environ.get("DISPLAY"), "resolution": screen_resolution()},
        "gpu_devices": devices,
        "qt": {},
        "renderer": {},
        "smoke": {},
        "warnings": [],
        "errors": [],
        "artifacts": {},
    }
    revision = run(["git", "rev-parse", "HEAD"])
    if revision.returncode == 0:
        report["commit"] = revision.stdout.strip()

    if gpu_error is not None:
        report["status"] = "skipped" if args.allow_missing_gpu else "failed"
        report["skip_reason" if args.allow_missing_gpu else "error"] = gpu_error
        report["errors"] = [] if args.allow_missing_gpu else [gpu_error]
        write_report(report_path, report)
        print(json.dumps(report, indent=2, sort_keys=True))
        return 0 if args.allow_missing_gpu else 1

    if not report["display"]["server"]:
        report["error"] = "DISPLAY is unset; the GPU smoke requires an NVIDIA-backed X11 session"
        report["errors"] = [report["error"]]
        write_report(report_path, report)
        print(json.dumps(report, indent=2, sort_keys=True))
        return 1

    capture_dir.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment.update({
        "DATUMLOOPDS_GPU_SMOKE": "1",
        "DATUMLOOPDS_SOFTWARE_GL": "0",
        "DATUMLOOPDS_GPU_CAPTURE_DIR": str(capture_dir),
        "QT_QPA_PLATFORM": "xcb",
        "QSG_RHI_BACKEND": "opengl",
        "QSG_INFO": "1",
    })
    environment.pop("LIBGL_ALWAYS_SOFTWARE", None)
    pointer_smoke = subprocess.run(
        [sys.executable, str(POINTER_SMOKE)],
        cwd=ROOT,
        env=environment,
        text=True,
        capture_output=True,
        timeout=25,
        check=False,
    )
    output = pointer_smoke.stdout + pointer_smoke.stderr
    values = parse_runtime_values(output)
    warnings = [line.strip() for line in output.splitlines() if re.search(r"\bwarning\b", line, re.IGNORECASE)]
    errors = [line.strip() for line in output.splitlines()
              if re.search(r"\b(error|critical|fatal)\b", line, re.IGNORECASE)]
    pointer_passed = "Pointer-driven object/face pick, camera navigation, and Move cancel/confirm/undo/redo passed." in output
    geometry_pixels = int(values.get("CAD_RENDER_PIXELS", "0"))
    overlay_triangles = int(values.get("FIELD_OVERLAY_TRIANGLES", "0"))
    changed_pixels = int(values.get("FIELD_OVERLAY_CHANGED_PIXELS", "0"))
    frame_count = int(values.get("PRESENTED_FRAMES", "0"))
    graphics_vendor = values.get("GRAPHICS_VENDOR", "")
    graphics_renderer = values.get("GRAPHICS_RENDERER", "")
    api = values.get("GRAPHICS_API", "")
    cad_frame = capture_dir / "cad-geometry.png"
    field_frame = capture_dir / "von-mises-overlay.png"
    window_resolution = values.get("WINDOW_SIZE")
    validations = {
        "cad_rendering": values.get("CAD_RENDER_VISIBLE") == "1" and geometry_pixels > 100 and frame_count > 0,
        "selection_and_camera": pointer_passed,
        "field_overlay": values.get("FIELD_OVERLAY_VISIBLE") == "1" and
                         values.get("FIELD_OVERLAY_MODE") == "von Mises Stress" and
                         overlay_triangles > 0 and changed_pixels > 100,
        "resolution_recorded": screen_resolution() is not None and window_resolution is not None,
        "viewport_frames_saved": cad_frame.is_file() and field_frame.is_file() and
                                 values.get("CAD_FRAME_SAVED") == "1" and values.get("FIELD_FRAME_SAVED") == "1",
        "nvidia_hardware_renderer": api == "OpenGL" and "nvidia" in graphics_vendor.lower() and
                                    "nvidia" in graphics_renderer.lower(),
    }
    if pointer_smoke.returncode != 0:
        errors.append(f"native_pointer_smoke.py exited with status {pointer_smoke.returncode}")
    for name, passed in validations.items():
        if not passed:
            errors.append(f"GPU viewport validation did not pass: {name}")

    report.update({
        "status": "passed" if not errors else "failed",
        "display": {
            "server": os.environ.get("DISPLAY"),
            "resolution": screen_resolution(),
            "screen_geometry": values.get("SCREEN_SIZE"),
            "window_resolution": window_resolution,
        },
        "qt": {
            "version": values.get("QT_VERSION"),
            "rhi_backend": api,
            "requested_rhi_backend": environment["QSG_RHI_BACKEND"],
        },
        "renderer": {
            "vendor": graphics_vendor,
            "name": graphics_renderer,
            "version": values.get("GRAPHICS_VERSION"),
        },
        "smoke": {
            "presented_frames": frame_count,
            "cad_render_pixels_differing_from_viewport_background": geometry_pixels,
            "field_overlay_triangles": overlay_triangles,
            "field_overlay_changed_pixels": changed_pixels,
            "validations": validations,
            "pointer_smoke_exit_code": pointer_smoke.returncode,
        },
        "warnings": warnings,
        "errors": errors,
        "runtime_log": output.splitlines(),
        "artifacts": {
            "cad_geometry_frame": str(capture_dir / "cad-geometry.png"),
            "von_mises_overlay_frame": str(capture_dir / "von-mises-overlay.png"),
        },
    })
    write_report(report_path, report)
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
