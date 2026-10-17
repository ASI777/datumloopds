#!/usr/bin/env python3
"""Compare OCCT and SMLib benchmark JSON files and write JSON plus Markdown."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


VOLUME_REL_TOLERANCE = 1e-4
AREA_REL_TOLERANCE = 1e-4
BBOX_ABS_TOLERANCE_MM = 1e-4
CENTER_OF_MASS_ABS_TOLERANCE_MM = 1e-4


def _bbox_delta(left: dict, right: dict) -> float | None:
    try:
        a = left["bounding_box_mm"]
        b = right["bounding_box_mm"]
        return max(
            abs(float(x) - float(y))
            for bound in ("minimum", "maximum")
            for x, y in zip(a[bound], b[bound])
        )
    except (KeyError, TypeError, ValueError):
        return None


def _point_delta(left: dict, right: dict, key: str) -> float | None:
    try:
        return max(abs(float(x) - float(y)) for x, y in zip(left[key], right[key]))
    except (KeyError, TypeError, ValueError):
        return None


def _compare_rows(left_rows: list[dict], right_rows: list[dict]) -> list[dict]:
    left_cases = {case["name"]: case for case in left_rows}
    right_cases = {case["name"]: case for case in right_rows}
    comparisons = []
    for name in sorted(set(left_cases) | set(right_cases)):
        a, b = left_cases.get(name), right_cases.get(name)
        row = {"name": name, "occt_success": bool(a and a.get("operation_success")),
               "smlib_success": bool(b and b.get("operation_success"))}
        if not a or not b or not row["occt_success"] or not row["smlib_success"]:
            row["geometry_within_tolerance"] = False
            row["topology_counts_match"] = False
            row["status"] = "one or both kernels failed"
            comparisons.append(row)
            continue

        av, bv = a.get("volume_mm3"), b.get("volume_mm3")
        aa, ba = a.get("surface_area_mm2"), b.get("surface_area_mm2")
        volume_delta = abs(av - bv) if av is not None and bv is not None else None
        area_delta = abs(aa - ba) if aa is not None and ba is not None else None
        volume_rel = volume_delta / max(abs(av), abs(bv), 1e-12) if volume_delta is not None else None
        area_rel = area_delta / max(abs(aa), abs(ba), 1e-12) if area_delta is not None else None
        bbox_delta = _bbox_delta(a, b)
        center_delta = _point_delta(a, b, "center_of_mass_mm")
        left_topology, right_topology = a.get("topology_counts"), b.get("topology_counts")
        topology_match = bool(left_topology and right_topology and left_topology == right_topology)
        geometry_ok = (
            volume_rel is not None and volume_rel <= VOLUME_REL_TOLERANCE
            and area_rel is not None and area_rel <= AREA_REL_TOLERANCE
            and bbox_delta is not None and bbox_delta <= BBOX_ABS_TOLERANCE_MM
            and center_delta is not None and center_delta <= CENTER_OF_MASS_ABS_TOLERANCE_MM
            and a.get("valid") is True and b.get("valid") is True
        )
        row.update({
            "status": "matched within tolerance" if geometry_ok else "geometry differs beyond tolerance",
            "occt_volume_mm3": av,
            "smlib_volume_mm3": bv,
            "volume_delta_mm3": volume_delta,
            "volume_relative_delta": volume_rel,
            "surface_area_delta_mm2": area_delta,
            "surface_area_relative_delta": area_rel,
            "bounding_box_max_delta_mm": bbox_delta,
            "center_of_mass_max_delta_mm": center_delta,
            "occt_topology_counts": left_topology,
            "smlib_topology_counts": right_topology,
            "topology_counts_match": topology_match,
            "occt_valid": a.get("valid"),
            "smlib_valid": b.get("valid"),
            "occt_triangle_count": (a.get("tessellation") or {}).get("triangle_count"),
            "smlib_triangle_count": (b.get("tessellation") or {}).get("triangle_count"),
            "occt_operation_time_ms": a.get("operation_time_ms", a.get("import_time_ms")),
            "smlib_operation_time_ms": b.get("operation_time_ms", b.get("import_time_ms")),
            "occt_tessellation_time_ms": (a.get("tessellation") or {}).get("time_ms"),
            "smlib_tessellation_time_ms": (b.get("tessellation") or {}).get("time_ms"),
            "geometry_within_tolerance": geometry_ok,
        })
        comparisons.append(row)
    return comparisons


def compare(occt: dict, smlib: dict) -> dict:
    occt_capabilities = occt.get("capabilities", {})
    smlib_capabilities = smlib.get("capabilities", {})
    return {
        "schema_version": 1,
        "occt_backend_version": occt.get("backend_version"),
        "smlib_backend_version": smlib.get("backend_version"),
        "capabilities": {
            "occt": occt_capabilities,
            "smlib": smlib_capabilities,
            "differences": {
                name: {"occt": occt_capabilities.get(name, False), "smlib": smlib_capabilities.get(name, False)}
                for name in sorted(set(occt_capabilities) | set(smlib_capabilities))
                if occt_capabilities.get(name, False) != smlib_capabilities.get(name, False)
            },
        },
        "tolerances": {
            "volume_relative": VOLUME_REL_TOLERANCE,
            "surface_area_relative": AREA_REL_TOLERANCE,
            "bounding_box_absolute_mm": BBOX_ABS_TOLERANCE_MM,
            "center_of_mass_absolute_mm": CENTER_OF_MASS_ABS_TOLERANCE_MM,
        },
        "cases": _compare_rows(occt.get("cases", []), smlib.get("cases", [])),
        "step_parts": _compare_rows(occt.get("step_parts", []), smlib.get("step_parts", [])),
    }


def markdown(report: dict) -> str:
    lines = [
        "# Cross-kernel geometry benchmark",
        "",
        f"OCCT / FreeCAD **{report.get('occt_backend_version')}** · SMLib **{report.get('smlib_backend_version')}**",
        "",
        "The suite uses the same dimensions and exact operations in both kernels. Geometry match uses relative volume and surface-area tolerances of 1e-4, absolute bounding-box and center-of-mass tolerances of 1e-4 mm, and valid B-Reps in both results. Exact topology counts are reported as a separate comparison because equivalent kernels can split faces differently. Timings are measurements from the current machine and are not expected to repeat exactly.",
        "",
        "## Capability comparison",
        "",
        "| Capability | OCCT | SMLib |",
        "|---|---:|---:|",
    ]
    for name in sorted(set(report["capabilities"]["occt"]) | set(report["capabilities"]["smlib"])):
        lines.append(
            f"| {name} | {'supported' if report['capabilities']['occt'].get(name, False) else 'unsupported'} | {'supported' if report['capabilities']['smlib'].get(name, False) else 'unsupported'} |"
        )
    lines.extend([
        "",
        "FreeCAD's Part tessellator accepts chord tolerance but not an angular tolerance; smlibpy receives both requested values. Triangle counts are reported as an approximate display comparison.",
        "",
        "| Case | Match | Topology counts | OCCT volume (mm³) | SMLib volume (mm³) | Δ volume (mm³) | Max Δ bounds (mm) | Triangles OCCT / SMLib | Model/import time ms OCCT / SMLib |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|",
    ])
    for row in report["cases"]:
        if not row.get("occt_success") or not row.get("smlib_success"):
            lines.append(f"| {row['name']} | failed | — | — | — | — | — | — |")
            continue
        lines.append(
            "| {name} | {match} | {topology} | {ov:.6g} | {sv:.6g} | {dv:.6g} | {bbox:.6g} | {ot} / {st} | {omt:.3f} / {smt:.3f} |".format(
                name=row["name"], match="yes" if row.get("geometry_within_tolerance") else "no",
                topology="same" if row.get("topology_counts_match") else "different",
                ov=row.get("occt_volume_mm3", float("nan")), sv=row.get("smlib_volume_mm3", float("nan")),
                dv=row.get("volume_delta_mm3", float("nan")), bbox=row.get("bounding_box_max_delta_mm", float("nan")),
                ot=row.get("occt_triangle_count", "—"), st=row.get("smlib_triangle_count", "—"),
                omt=row.get("occt_operation_time_ms", float("nan")), smt=row.get("smlib_operation_time_ms", float("nan")),
            )
        )
    if report.get("step_parts"):
        lines.extend(["", "## Imported machine STEP parts", "", "| Part | Match | Topology counts | Volume Δ (mm³) | Max Δ bounds (mm) | Max Δ center (mm) |", "|---|---:|---:|---:|---:|---:|"])
        for row in report["step_parts"]:
            if not row.get("occt_success") or not row.get("smlib_success"):
                lines.append(f"| {row['name']} | failed | — | — | — | — |")
                continue
            lines.append(
                "| {name} | {match} | {topology} | {volume:.6g} | {bounds:.6g} | {center:.6g} |".format(
                    name=row["name"],
                    match="yes" if row.get("geometry_within_tolerance") else "no",
                    topology="same" if row.get("topology_counts_match") else "different",
                    volume=row.get("volume_delta_mm3", float("nan")),
                    bounds=row.get("bounding_box_max_delta_mm", float("nan")),
                    center=row.get("center_of_mass_max_delta_mm", float("nan")),
                )
            )
    lines.extend(["", "STEP round-trip timings and deltas are recorded in the per-backend JSON files.", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--occt", required=True)
    parser.add_argument("--smlib", required=True)
    parser.add_argument("--json", required=True)
    parser.add_argument("--markdown", required=True)
    args = parser.parse_args()
    report = compare(json.loads(Path(args.occt).read_text()), json.loads(Path(args.smlib).read_text()))
    json_path, markdown_path = Path(args.json), Path(args.markdown)
    json_path.parent.mkdir(parents=True, exist_ok=True)
    markdown_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    markdown_path.write_text(markdown(report), encoding="utf-8")
    rows = report["cases"] + report["step_parts"]
    matched = sum(bool(case.get("geometry_within_tolerance")) for case in rows)
    print(f"geometry comparison: {matched}/{len(rows)} rows matched tolerances")
    print(f"JSON: {json_path}")
    print(f"Markdown: {markdown_path}")
    return 0 if matched == len(rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
