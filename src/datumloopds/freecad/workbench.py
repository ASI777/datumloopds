"""FreeCAD workbench registration and project commands."""

from __future__ import annotations

import FreeCADGui as Gui

from datumloopds.freecad import project_actions, shell


class _WorkbenchCommand:
    def __init__(self, label: str, tooltip: str, action):
        self.label = label
        self.tooltip = tooltip
        self.action = action

    def GetResources(self):
        return {"MenuText": self.label, "ToolTip": self.tooltip}

    def Activated(self):
        self.action()

    def IsActive(self):
        return True


class DatumLoopWorkbench(Gui.Workbench):
    MenuText = "DatumLoop DS"
    ToolTip = "Industrial mechanism design and simulation"
    Icon = str(shell.icon_path())

    def Initialize(self):
        Gui.addCommand(
            "DatumLoopDS_NewProject",
            _WorkbenchCommand("New Project", "Create a DatumLoop DS FreeCAD project", shell._show_new_project),
        )
        Gui.addCommand(
            "DatumLoopDS_OpenProject",
            _WorkbenchCommand("Open Project", "Open a FreeCAD project document", shell._show_open_project),
        )
        Gui.addCommand(
            "DatumLoopDS_SaveProject",
            _WorkbenchCommand("Save Project", "Save the active FreeCAD project", shell._show_save_project),
        )
        self.appendToolbar("DatumLoop DS", [
            "DatumLoopDS_NewProject",
            "DatumLoopDS_OpenProject",
            "DatumLoopDS_SaveProject",
        ])
        self.appendMenu("Project", [
            "DatumLoopDS_NewProject",
            "DatumLoopDS_OpenProject",
            "DatumLoopDS_SaveProject",
        ])

    def Activated(self):
        shell.install_shell()

    def GetClassName(self):
        return "Gui::PythonWorkbench"


def register():
    Gui.addWorkbench(DatumLoopWorkbench())
