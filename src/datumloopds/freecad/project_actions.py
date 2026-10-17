"""Create, open, and save DatumLoop DS projects in the FreeCAD document API."""

from __future__ import annotations

import json
from pathlib import Path

import FreeCAD as App
from PySide import QtWidgets

from datumloopds.project import new_project_metadata


PROJECT_OBJECT_NAME = "DatumLoopProject"


def create_project(name: str):
    """Create a native FreeCAD document and persist DatumLoop project metadata."""

    metadata = new_project_metadata(name)
    doc = App.newDocument("DatumLoopProject")
    doc.Label = metadata.name
    project = doc.addObject("App::DocumentObjectGroup", PROJECT_OBJECT_NAME)
    project.Label = "DatumLoop Project"
    project.addProperty("App::PropertyString", "ProjectId", "DatumLoop")
    project.addProperty("App::PropertyInteger", "SchemaVersion", "DatumLoop")
    project.addProperty("App::PropertyString", "CreatedUTC", "DatumLoop")
    project.addProperty("App::PropertyString", "ProjectMetadataJSON", "DatumLoop")
    project.ProjectId = metadata.project_id
    project.SchemaVersion = metadata.schema_version
    project.CreatedUTC = metadata.created_utc
    project.ProjectMetadataJSON = json.dumps(metadata.to_dict(), sort_keys=True)
    doc.recompute()
    return doc


def open_project(parent=None):
    path, _ = QtWidgets.QFileDialog.getOpenFileName(
        parent,
        "Open DatumLoop DS Project",
        "",
        "FreeCAD documents (*.FCStd *.FCStd1);;All files (*)",
    )
    if not path:
        return None
    return App.openDocument(path)


def save_project(parent=None):
    doc = App.ActiveDocument
    if doc is None:
        return None

    current_path = getattr(doc, "FileName", "")
    if current_path:
        doc.save()
        return current_path

    path, _ = QtWidgets.QFileDialog.getSaveFileName(
        parent,
        "Save DatumLoop DS Project",
        f"{doc.Label}.FCStd",
        "FreeCAD documents (*.FCStd)",
    )
    if not path:
        return None
    if Path(path).suffix.lower() != ".fcstd":
        path += ".FCStd"
    doc.saveAs(path)
    return path
