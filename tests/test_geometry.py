import importlib.util
import tempfile
import unittest
from pathlib import Path

from datumloopds.geometry import (
    Axis3D,
    BoundingBox,
    GeometryShape,
    KernelCapabilities,
    TriangleMesh,
    TopologyCounts,
    UnsupportedKernelOperation,
    create_kernel,
)
from datumloopds.geometry.conversion import convert_shape


class GeometryValueTests(unittest.TestCase):
    def test_axis_direction_is_normalized(self):
        self.assertEqual(Axis3D(direction=(0, 0, 8)).direction, (0.0, 0.0, 1.0))

    def test_axis_rejects_zero_direction(self):
        with self.assertRaisesRegex(ValueError, "must not be zero"):
            Axis3D(direction=(0, 0, 0))

    def test_bounding_box_has_portable_size_and_center(self):
        bounds = BoundingBox((-2, -4, -6), (2, 4, 6))
        self.assertEqual(bounds.size, (4.0, 8.0, 12.0))
        self.assertEqual(bounds.center, (0.0, 0.0, 0.0))

    def test_triangle_mesh_validates_indices(self):
        mesh = TriangleMesh(((0, 0, 0), (1, 0, 0), (0, 1, 0)), ((0, 1, 2),))
        self.assertEqual(mesh.triangle_count, 1)
        with self.assertRaisesRegex(ValueError, "outside the vertex array"):
            TriangleMesh(((0, 0, 0),), ((0, 1, 0),))

    def test_topology_counts_are_portable_values(self):
        self.assertEqual(TopologyCounts(1, 6, 12, 8).solids, 1)

    def test_unknown_backend_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "choose 'occt' or 'smlib'"):
            create_kernel("mesh")

    def test_step_conversion_checks_capabilities_before_io(self):
        shape = GeometryShape("fake", object(), object())

        class Backend:
            name = "fake"
            capabilities = KernelCapabilities(step_export=False)

        with self.assertRaises(UnsupportedKernelOperation) as caught:
            convert_shape(shape, Backend(), Backend())
        self.assertEqual(caught.exception.operation, "step_export")


@unittest.skipUnless(importlib.util.find_spec("smlibpy"), "optional smlibpy package is not installed")
class SMLibBackendTests(unittest.TestCase):
    def test_exact_modeling_metrics_and_file_hooks(self):
        kernel = create_kernel("smlib")
        self.assertTrue(kernel.capabilities.usd_brep)
        self.assertTrue(kernel.capabilities.step_import)
        self.assertTrue(kernel.capabilities.step_export)
        self.assertTrue(kernel.capabilities.fillet)
        self.assertTrue(kernel.capabilities.offset)

        box = kernel.box(20, 20, 20)
        cylinder = kernel.cylinder(3, 30)
        drilled = kernel.cut(box, cylinder)
        rounded = kernel.fillet(box, 1)
        self.assertAlmostEqual(kernel.volume(box), 8000, places=5)
        self.assertLess(kernel.volume(drilled), kernel.volume(box))
        self.assertLess(kernel.volume(rounded), kernel.volume(box))
        self.assertTrue(kernel.is_valid(drilled))
        self.assertEqual(kernel.bounding_box(box).minimum, (-10.0, -10.0, -10.0))
        self.assertGreater(kernel.tessellate(drilled, tolerance=0.2).triangle_count, 0)
        counts = kernel.topology_counts(box)
        self.assertGreater(counts.solids, 0)
        self.assertGreater(counts.faces, 0)
        self.assertGreater(counts.edges, 0)
        self.assertGreater(counts.vertices, 0)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            restored = kernel.deserialize_brep(kernel.serialize_brep(box))
            self.assertAlmostEqual(kernel.volume(restored), 8000, places=5)
            kernel.export_step((box,), root / "box.step")
            self.assertAlmostEqual(kernel.volume(kernel.import_step(root / "box.step")[0]), 8000, places=5)
            kernel.export_usd_brep((box,), root / "box.usda")
            self.assertIn("BrepArray", (root / "box.usda").read_text())
            self.assertAlmostEqual(kernel.volume(kernel.import_usd_brep(root / "box.usda")[0]), 8000, places=5)


if __name__ == "__main__":
    unittest.main()
