import unittest

from datumloopds.project import (
    PROJECT_SCHEMA_VERSION,
    new_project_metadata,
    validate_project_metadata,
)


class ProjectMetadataTests(unittest.TestCase):
    def test_factory_creates_reproducible_metadata_from_fixture_values(self):
        metadata = new_project_metadata(
            "  Axis Study  ",
            project_id="9a57f38e-4e60-42f9-a978-7daec182d3c2",
            created_utc="2026-10-17T12:00:00Z",
        )

        self.assertEqual(metadata.name, "Axis Study")
        self.assertEqual(metadata.schema_version, PROJECT_SCHEMA_VERSION)
        self.assertEqual(metadata.project_id, "9a57f38e-4e60-42f9-a978-7daec182d3c2")
        self.assertEqual(metadata.created_utc, "2026-10-17T12:00:00Z")

    def test_factory_rejects_empty_name(self):
        with self.assertRaisesRegex(ValueError, "project name"):
            new_project_metadata("   ")

    def test_validator_rejects_unknown_schema(self):
        metadata = new_project_metadata("Axis Study").to_dict()
        metadata["schema_version"] = PROJECT_SCHEMA_VERSION + 1

        with self.assertRaisesRegex(ValueError, "unsupported project schema"):
            validate_project_metadata(metadata)

    def test_validator_requires_timezone(self):
        metadata = new_project_metadata("Axis Study").to_dict()
        metadata["created_utc"] = "2026-10-17T12:00:00"

        with self.assertRaisesRegex(ValueError, "include a timezone"):
            validate_project_metadata(metadata)


if __name__ == "__main__":
    unittest.main()
