"""Deterministic exact-geometry fixtures used by the cross-kernel benchmark."""

from __future__ import annotations

import json
import platform
import sys
import tempfile
import time
from dataclasses import asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable

from . import create_kernel
from .types import Axis3D, GeometryShape, Point3


TESS_TOLERANCE_MM = 0.1
TESS_ANGLE_DEGREES = 15.0


def step_file_specs_from_json(payload: str | None) -> tuple[tuple[str, Path], ...]:
    """Parse optional named STEP inputs passed to both isolated runtimes."""

    if not payload:
        return ()
    values = json.loads(payload)
    if not isinstance(values, list):
        raise ValueError("STEP benchmark inputs must be a JSON array")
    specs = []
    for item in values:
        if not isinstance(item, dict) or not isinstance(item.get("name"), str) or not isinstance(item.get("path"), str):
            raise ValueError("each STEP benchmark input must contain string 'name' and 'path' fields")
        path = Path(item["path"])
        if not path.is_file():
            raise FileNotFoundError(f"STEP benchmark input does not exist: {path}")
        specs.append((item["name"], path))
    return tuple(specs)


def _profile(kernel, points: tuple[Point3, ...]) -> GeometryShape:
    z_values = [point[2] for point in points]
    if max(z_values) - min(z_values) > 1e-9:
        raise ValueError("benchmark polygon profiles must lie in a plane parallel to XY")
    if kernel.name == "smlib":
        import smlibpy

        native = smlibpy.Polygon(*[(x, y) for x, y, _ in points])
        return kernel.wrap_native(native.translate(0, 0, z_values[0]))
    import FreeCAD as App
    import Part

    vertices = [App.Vector(*point) for point in points]
    wire = Part.makePolygon(vertices + [vertices[0]])
    return kernel.wrap_native(Part.Face(wire))


def _circle_profile(kernel, radius: float) -> GeometryShape:
    if kernel.name == "smlib":
        import smlibpy

        return kernel.wrap_native(smlibpy.Circle(radius))
    import FreeCAD as App
    import Part

    edge = Part.makeCircle(radius, App.Vector(0, 0, 0), App.Vector(0, 0, 1))
    return kernel.wrap_native(Part.Wire([edge]))


def _line_path(kernel, start: Point3, end: Point3) -> GeometryShape:
    if kernel.name == "smlib":
        import smlibpy

        return kernel.wrap_native(smlibpy.Line(start, end))
    import FreeCAD as App
    import Part

    edge = Part.makeLine(App.Vector(*start), App.Vector(*end))
    return kernel.wrap_native(Part.Wire([edge]))


def _build_cases(kernel) -> tuple[tuple[str, Callable[[], tuple[GeometryShape, dict]]], ...]:
    def box_case():
        return kernel.box(60, 40, 20), {"construction": ["box"]}

    def cylinder_case():
        return kernel.cylinder(12, 45), {"construction": ["cylinder"]}

    def through_hole_case():
        plate = kernel.box(60, 40, 8)
        drill = kernel.cylinder(5, 12)
        return kernel.cut(plate, drill), {"construction": ["box", "cylinder", "cut"]}

    def filleted_block_case():
        block = kernel.box(60, 40, 20)
        return kernel.fillet(block, 2), {"construction": ["box", "fillet(all edges)"]}

    def revolved_spindle_case():
        profile = _profile(kernel, ((10, -20, 0), (20, -20, 0), (20, 20, 0), (10, 20, 0)))
        spindle = kernel.revolve(profile, Axis3D(direction=(0, 1, 0)))
        return spindle, {"construction": ["polygon profile", "revolve"]}

    def swept_tube_case():
        profile = _circle_profile(kernel, 2)
        path = _line_path(kernel, (0, 0, 0), (0, 0, 40))
        return kernel.sweep(profile, path), {"construction": ["circle profile", "line path", "sweep"]}

    def bracket_case():
        base = kernel.box(60, 40, 8, center=(0, 0, 4))
        # The upright extends four millimetres into the base so both kernels
        # see a volumetric joint rather than a face-only contact.
        upright = kernel.box(8, 40, 40, center=(-26, 0, 24))
        overlap = kernel.intersection(base, upright)
        joined = kernel.union(base, upright)
        drills = tuple(
            kernel.cylinder(3.5, 12, center=(x, y, 4))
            for x, y in ((-20, -10), (-20, 10), (20, -10), (20, 10))
        )
        slot = kernel.box(10, 12, 20, center=(-26, 0, 24))
        bracket = kernel.cut(joined, *drills, slot)
        return bracket, {
            "construction": ["base/upright", "intersection", "union", "four through-holes", "slot cut"],
            "base_upright_overlap_volume_mm3": kernel.volume(overlap),
        }

    return (
        ("box", box_case),
        ("cylinder", cylinder_case),
        ("box_with_cylindrical_through_hole", through_hole_case),
        ("filleted_rectangular_block", filleted_block_case),
        ("revolved_spindle", revolved_spindle_case),
        ("swept_tube", swept_tube_case),
        ("multiple_boolean_bracket", bracket_case),
    )


def _backend_version(kernel_name: str) -> str:
    if kernel_name == "smlib":
        import smlibpy

        return smlibpy.__version__
    import FreeCAD as App

    return ".".join(App.Version()[:3])


def _case_report(kernel, name: str, build: Callable[[], tuple[GeometryShape, dict]]) -> dict:
    started = time.perf_counter()
    try:
        shape, details = build()
    except Exception as exc:
        return {
            "name": name,
            "operation_success": False,
            "operation_time_ms": (time.perf_counter() - started) * 1000,
            "error_type": type(exc).__name__,
            "error": str(exc),
        }
    operation_time_ms = (time.perf_counter() - started) * 1000
    result = {
        "name": name,
        "operation_success": True,
        "operation_time_ms": operation_time_ms,
        "details": details,
    }
    try:
        bounds = kernel.bounding_box(shape)
        result.update({
            "volume_mm3": kernel.volume(shape),
            "surface_area_mm2": kernel.surface_area(shape),
            "center_of_mass_mm": kernel.center_of_mass(shape),
            "topology_counts": asdict(kernel.topology_counts(shape)),
            "bounding_box_mm": {"minimum": bounds.minimum, "maximum": bounds.maximum},
            "valid": kernel.is_valid(shape),
        })
    except Exception as exc:
        result["measurement_error"] = f"{type(exc).__name__}: {exc}"

    try:
        tess_start = time.perf_counter()
        mesh = kernel.tessellate(shape, tolerance=TESS_TOLERANCE_MM, angle_degrees=TESS_ANGLE_DEGREES)
        result["tessellation"] = {
            "time_ms": (time.perf_counter() - tess_start) * 1000,
            "triangle_count": mesh.triangle_count,
            "requested_chord_tolerance_mm": TESS_TOLERANCE_MM,
            "requested_angle_degrees": TESS_ANGLE_DEGREES,
        }
    except Exception as exc:
        result["tessellation"] = {"error": f"{type(exc).__name__}: {exc}"}

    if kernel.capabilities.step_export and kernel.capabilities.step_import:
        try:
            with tempfile.TemporaryDirectory(prefix="datumloopds-benchmark-step-") as temp_dir:
                step_path = Path(temp_dir) / f"{name}.step"
                step_start = time.perf_counter()
                kernel.export_step((shape,), step_path)
                imported = kernel.import_step(step_path)
                roundtrip_volume = sum(kernel.volume(item) for item in imported)
                result["step_roundtrip"] = {
                    "time_ms": (time.perf_counter() - step_start) * 1000,
                    "shape_count": len(imported),
                    "volume_mm3": roundtrip_volume,
                    "volume_delta_mm3": abs(roundtrip_volume - result.get("volume_mm3", 0.0)),
                }
        except Exception as exc:
            result["step_roundtrip"] = {"error": f"{type(exc).__name__}: {exc}"}
    else:
        result["step_roundtrip"] = {"status": "unsupported"}
    return result


def _merge_bounds(bounds_list):
    minimum = tuple(min(bounds.minimum[axis] for bounds in bounds_list) for axis in range(3))
    maximum = tuple(max(bounds.maximum[axis] for bounds in bounds_list) for axis in range(3))
    return {"minimum": minimum, "maximum": maximum}


def _step_part_report(kernel, name: str, path: str | Path) -> dict:
    """Inspect an existing STEP part without modifying or copying its source."""

    source = Path(path)
    started = time.perf_counter()
    row = {
        "name": name,
        "source_filename": source.name,
        "source_size_bytes": source.stat().st_size,
        "operation_success": False,
    }
    try:
        shapes = kernel.import_step(source)
        row["import_time_ms"] = (time.perf_counter() - started) * 1000
        row["shape_count"] = len(shapes)
        if not shapes:
            raise ValueError("STEP import returned no shapes")

        bounds = [kernel.bounding_box(shape) for shape in shapes]
        topology = [kernel.topology_counts(shape) for shape in shapes]
        volumes = [kernel.volume(shape) for shape in shapes]
        centers = [kernel.center_of_mass(shape) for shape in shapes]
        total_volume = sum(volumes)
        if total_volume > 0:
            center = tuple(
                sum(volume * point[axis] for volume, point in zip(volumes, centers)) / total_volume
                for axis in range(3)
            )
        else:
            center = None
        row.update({
            "operation_success": True,
            "valid": all(kernel.is_valid(shape) for shape in shapes),
            "volume_mm3": total_volume,
            "surface_area_mm2": sum(kernel.surface_area(shape) for shape in shapes),
            "center_of_mass_mm": center,
            "topology_counts": {
                field: sum(getattr(count, field) for count in topology)
                for field in ("solids", "faces", "edges", "vertices")
            },
            "bounding_box_mm": _merge_bounds(bounds),
        })
        tess_start = time.perf_counter()
        tessellations = [
            kernel.tessellate(shape, tolerance=TESS_TOLERANCE_MM, angle_degrees=TESS_ANGLE_DEGREES)
            for shape in shapes
        ]
        row["tessellation"] = {
            "time_ms": (time.perf_counter() - tess_start) * 1000,
            "triangle_count": sum(mesh.triangle_count for mesh in tessellations),
        }
        with tempfile.TemporaryDirectory(prefix="datumloopds-step-part-") as temp_dir:
            step_path = Path(temp_dir) / source.name
            roundtrip_started = time.perf_counter()
            kernel.export_step(shapes, step_path)
            restored = kernel.import_step(step_path)
            roundtrip_volume = sum(kernel.volume(shape) for shape in restored)
            row["step_roundtrip"] = {
                "time_ms": (time.perf_counter() - roundtrip_started) * 1000,
                "shape_count": len(restored),
                "volume_mm3": roundtrip_volume,
                "volume_delta_mm3": abs(roundtrip_volume - total_volume),
            }
    except Exception as exc:
        row["error_type"] = type(exc).__name__
        row["error"] = str(exc)
    return row


def run_benchmark(
    backend_name: str,
    output_path: str | Path,
    *,
    step_files: tuple[tuple[str, str | Path], ...] = (),
) -> dict:
    """Run the fixed model fixtures and write one backend's JSON measurements."""

    kernel = create_kernel(backend_name)
    report = {
        "schema_version": 1,
        "created_utc": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "backend": kernel.name,
        "backend_version": _backend_version(kernel.name),
        "python_version": platform.python_version(),
        "platform": platform.platform(),
        "units": {"length": "mm", "volume": "mm^3", "area": "mm^2"},
        "capabilities": asdict(kernel.capabilities),
        "tessellation": {
            "chord_tolerance_mm": TESS_TOLERANCE_MM,
            "angle_degrees": TESS_ANGLE_DEGREES,
            "note": "FreeCAD Part.tessellate accepts chord tolerance only; SMLib also receives the requested angle.",
        },
        "cases": [_case_report(kernel, name, builder) for name, builder in _build_cases(kernel)],
        "step_parts": [_step_part_report(kernel, name, path) for name, path in step_files],
    }
    output = Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return report


def all_cases_succeeded(report: dict) -> bool:
    return all(
        case.get("operation_success") is True
        and case.get("valid") is True
        and "measurement_error" not in case
        for case in report["cases"]
    ) and all(
        part.get("operation_success") is True
        and part.get("valid") is True
        and "error" not in part
        for part in report.get("step_parts", [])
    )
