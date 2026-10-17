import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from compare_geometry_benchmarks import compare


def _backend(topology, *, usd_brep):
    return {
        "backend_version": "test",
        "capabilities": {"usd_brep": usd_brep},
        "cases": [{
            "name": "shape",
            "operation_success": True,
            "volume_mm3": 10.0,
            "surface_area_mm2": 8.0,
            "center_of_mass_mm": (1.0, 2.0, 3.0),
            "bounding_box_mm": {"minimum": (0.0, 0.0, 0.0), "maximum": (2.0, 2.0, 2.0)},
            "topology_counts": topology,
            "valid": True,
            "tessellation": {"triangle_count": 12, "time_ms": 1.0},
            "operation_time_ms": 1.0,
        }],
    }


class BenchmarkComparisonTests(unittest.TestCase):
    def test_geometry_match_is_independent_of_topology_split(self):
        report = compare(
            _backend({"solids": 1, "faces": 6, "edges": 12, "vertices": 8}, usd_brep=False),
            _backend({"solids": 1, "faces": 3, "edges": 6, "vertices": 2}, usd_brep=True),
        )
        self.assertTrue(report["cases"][0]["geometry_within_tolerance"])
        self.assertFalse(report["cases"][0]["topology_counts_match"])
        self.assertEqual(report["capabilities"]["differences"]["usd_brep"], {"occt": False, "smlib": True})

    def test_center_of_mass_is_in_geometry_tolerance(self):
        left = _backend({"solids": 1, "faces": 6, "edges": 12, "vertices": 8}, usd_brep=False)
        right = _backend({"solids": 1, "faces": 6, "edges": 12, "vertices": 8}, usd_brep=True)
        right["cases"][0]["center_of_mass_mm"] = (1.001, 2.0, 3.0)
        report = compare(left, right)
        self.assertFalse(report["cases"][0]["geometry_within_tolerance"])


if __name__ == "__main__":
    unittest.main()
