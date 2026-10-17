"""Portable metadata stored with a DatumLoop DS FreeCAD document."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from typing import Mapping, Any
from uuid import UUID, uuid4


PROJECT_SCHEMA_VERSION = 1


@dataclass(frozen=True)
class ProjectMetadata:
    schema_version: int
    project_id: str
    name: str
    created_utc: str

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


def new_project_metadata(
    name: str,
    *,
    project_id: str | None = None,
    created_utc: str | None = None,
) -> ProjectMetadata:
    """Create validated metadata, with stable inputs available for fixtures."""

    clean_name = name.strip()
    if not clean_name:
        raise ValueError("project name must not be empty")

    stable_id = str(UUID(project_id)) if project_id is not None else str(uuid4())
    timestamp = created_utc or datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace(
        "+00:00", "Z"
    )
    metadata = ProjectMetadata(
        schema_version=PROJECT_SCHEMA_VERSION,
        project_id=stable_id,
        name=clean_name,
        created_utc=timestamp,
    )
    validate_project_metadata(metadata.to_dict())
    return metadata


def validate_project_metadata(value: Mapping[str, Any]) -> ProjectMetadata:
    """Validate metadata read from a FreeCAD document or JSON payload."""

    try:
        schema_version = value["schema_version"]
        project_id = value["project_id"]
        name = value["name"]
        created_utc = value["created_utc"]
    except KeyError as exc:
        raise ValueError(f"missing project metadata field: {exc.args[0]}") from exc

    if isinstance(schema_version, bool) or not isinstance(schema_version, int):
        raise ValueError("schema_version must be an integer")
    if schema_version != PROJECT_SCHEMA_VERSION:
        raise ValueError(f"unsupported project schema version: {schema_version}")
    if not isinstance(project_id, str):
        raise ValueError("project_id must be a UUID string")
    if str(UUID(project_id)) != project_id.lower():
        raise ValueError("project_id must use canonical UUID formatting")
    if not isinstance(name, str) or not name.strip():
        raise ValueError("project name must not be empty")
    if not isinstance(created_utc, str):
        raise ValueError("created_utc must be an ISO 8601 string")
    try:
        parsed_timestamp = datetime.fromisoformat(created_utc.replace("Z", "+00:00"))
    except ValueError as exc:
        raise ValueError("created_utc must be an ISO 8601 timestamp") from exc
    if parsed_timestamp.tzinfo is None:
        raise ValueError("created_utc must include a timezone")
    if parsed_timestamp.utcoffset() != timezone.utc.utcoffset(parsed_timestamp):
        raise ValueError("created_utc must be in UTC")

    return ProjectMetadata(
        schema_version=schema_version,
        project_id=project_id.lower(),
        name=name.strip(),
        created_utc=created_utc,
    )
