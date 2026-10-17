from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NativeArchitectureBoundaryTests(unittest.TestCase):
    def test_exact_shape_is_document_state_and_renderer_accepts_only_display_mesh(self):
        document_header = (ROOT / "src/datumloop/core/DatumDocument.hpp").read_text()
        renderer_header = (ROOT / "src/datumloop/renderer/ViewportRenderer.hpp").read_text()
        quick3d_renderer = (ROOT / "src/datumloop/ui/viewport/QtQuick3DRenderer.cpp").read_text()

        self.assertIn("TopoDS_Shape shape;", document_header)
        self.assertNotIn("TriangleMesh", document_header)
        self.assertIn("setDisplayMesh(const TriangleMesh& mesh)", renderer_header)
        self.assertNotIn("TopoDS", renderer_header)
        self.assertNotIn("BRep", renderer_header)
        self.assertIn("geometry_->setMesh(mesh)", quick3d_renderer)
        self.assertNotIn("TopoDS", quick3d_renderer)

    def test_freecad_is_documented_as_compatibility_only(self):
        architecture = (ROOT / "docs/architecture.md").read_text().lower()
        self.assertIn("compatibility subsystem", architecture)
        self.assertIn("primary", architecture)
        self.assertIn("native", architecture)


if __name__ == "__main__":
    unittest.main()
