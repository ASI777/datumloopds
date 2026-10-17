import unittest
from pathlib import Path
from xml.etree import ElementTree


ROOT = Path(__file__).resolve().parents[1]
FC_NS = "https://wiki.freecad.org/Package_Metadata"


class FreeCADPackageTests(unittest.TestCase):
    def test_workbench_manifest_and_entrypoints_exist(self):
        package = ROOT / "freecad" / "DatumLoopDS"
        manifest = ElementTree.parse(package / "package.xml").getroot()

        self.assertEqual(manifest.findtext(f"{{{FC_NS}}}name"), "DatumLoopDS")
        workbench = manifest.find(f"{{{FC_NS}}}content/{{{FC_NS}}}workbench")
        self.assertIsNotNone(workbench)
        self.assertEqual(
            workbench.findtext(f"{{{FC_NS}}}classname"),
            "DatumLoopWorkbench",
        )
        self.assertTrue((package / "Init.py").is_file())
        self.assertTrue((package / "InitGui.py").is_file())
        self.assertTrue((package / "Launch.FCMacro").is_file())
        self.assertTrue((package / "resources/icons/datumloop.svg").is_file())

    def test_source_launcher_includes_engine_and_package_paths(self):
        launcher = (ROOT / "scripts/launch-freecad.sh").read_text()

        self.assertIn("-M \"$repo_root/freecad/DatumLoopDS\"", launcher)
        self.assertIn("-P \"$repo_root/src\"", launcher)
        self.assertIn("Launch.FCMacro", launcher)


if __name__ == "__main__":
    unittest.main()
