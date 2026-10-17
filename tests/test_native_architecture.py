from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NativeArchitectureBoundaryTests(unittest.TestCase):
    def test_exact_shape_is_document_state_and_renderer_accepts_only_display_mesh(self):
        document_header = (ROOT / "src/datumloop/core/DatumDocument.hpp").read_text()
        renderer_header = (ROOT / "src/datumloop/renderer/ViewportRenderer.hpp").read_text()
        renderer_impl_header = (ROOT / "src/datumloop/ui/viewport/QtQuick3DRenderer.hpp").read_text()
        quick3d_renderer = (ROOT / "src/datumloop/ui/viewport/QtQuick3DRenderer.cpp").read_text()
        app_main = (ROOT / "apps/datumloop-desktop/main.cpp").read_text()

        self.assertIn("TopoDS_Shape shape;", document_header)
        self.assertNotIn("TriangleMesh", document_header)
        self.assertIn("setDisplayMesh(const TriangleMesh& mesh)", renderer_header)
        self.assertNotIn("TopoDS", renderer_header)
        self.assertNotIn("BRep", renderer_header)
        self.assertIn("public IViewportRenderer", renderer_impl_header)
        self.assertIn("geometry_->setMesh(mesh)", quick3d_renderer)
        self.assertNotIn("TopoDS", quick3d_renderer)
        self.assertIn("QtQuick3DRenderer renderer(geometry)", app_main)
        self.assertIn("OcctKernel::tessellate(initialObject->shape)", app_main)

    def test_document_core_does_not_depend_on_qml_or_quick(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        core_target = cmake.split("add_library(datumloop_core STATIC", 1)[1].split(")", 1)[0]
        core_link = cmake.split("target_link_libraries(datumloop_core PUBLIC", 1)[1].split(")", 1)[0]

        self.assertNotIn("QML", core_target.upper())
        self.assertNotIn("QObject", core_target)
        self.assertNotIn("Qt6::", core_link)

    def test_shell_composes_explicit_editors_and_viewport_hits_use_picking_service(self):
        main_qml = (ROOT / "qml/Main.qml").read_text()
        workspace_qml = (ROOT / "qml/workspace/DatumWorkspace.qml").read_text()
        area_qml = (ROOT / "qml/workspace/DatumArea.qml").read_text()
        host_qml = (ROOT / "qml/workspace/EditorHost.qml").read_text()
        viewport_qml = (ROOT / "qml/editors/ViewportEditor.qml").read_text()
        picking_service = (ROOT / "src/datumloop/ui/PickingService.cpp").read_text()

        self.assertIn("DatumWorkspace {", main_qml)
        self.assertNotIn("View3D {", main_qml)
        self.assertIn("DatumArea {", workspace_qml)
        self.assertIn("EditorHost {", area_qml)
        self.assertIn("DatumEditorType.Properties", area_qml)
        self.assertIn("DatumEditorType.Outliner", host_qml)
        self.assertIn("DatumEditorType.Viewport", host_qml)
        self.assertIn("PropertiesEditor {", host_qml)
        self.assertIn("pickingService.resolveObjectHit", viewport_qml)
        self.assertIn("pickingService.resolveSubshapeHit", viewport_qml)
        self.assertIn("selectTopologyReference", picking_service)

    def test_freecad_is_documented_as_compatibility_only(self):
        architecture = (ROOT / "docs/architecture.md").read_text().lower()
        self.assertIn("compatibility subsystem", architecture)
        self.assertIn("primary", architecture)
        self.assertIn("native", architecture)


if __name__ == "__main__":
    unittest.main()
