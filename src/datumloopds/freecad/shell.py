"""DatumLoop DS workbench panels hosted inside FreeCAD's main window."""

from __future__ import annotations

from pathlib import Path

import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtGui, QtWidgets

from datumloopds.freecad import project_actions


APP_TITLE = "DatumLoop DS"
DOCK_OBJECT_NAMES = (
    "DatumLoopDS.ProjectNavigator",
    "DatumLoopDS.EngineeringPanel",
    "DatumLoopDS.StudyWorkspace",
)


def icon_path() -> Path:
    """Locate the icon in either a source checkout or installed workbench."""

    module_file = Path(__file__).resolve()
    candidates = (
        module_file.parents[2] / "resources" / "icons" / "datumloop.svg",
        module_file.parents[3] / "freecad" / "DatumLoopDS" / "resources" / "icons" / "datumloop.svg",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return candidates[-1]


def _main_window():
    return Gui.getMainWindow()


def _show_new_project():
    parent = _main_window()
    name, accepted = QtWidgets.QInputDialog.getText(
        parent, "New DatumLoop DS Project", "Project name:", text="Untitled Mechanism"
    )
    if not accepted:
        return
    project_actions.create_project(name)


def _show_open_project():
    project_actions.open_project(_main_window())


def _show_save_project():
    project_actions.save_project(_main_window())


class ProjectNavigator(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("DatumLoopDS.ProjectNavigatorContent")

        root = QtWidgets.QVBoxLayout(self)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(10)

        brand = QtWidgets.QLabel("DATUMLOOP <span style='color:#38d5c5'>DS</span>")
        brand.setObjectName("BrandMark")
        brand.setTextFormat(QtCore.Qt.TextFormat.RichText)
        root.addWidget(brand)

        self.project_label = QtWidgets.QLabel("No project open")
        self.project_label.setObjectName("ProjectName")
        self.project_label.setWordWrap(True)
        root.addWidget(self.project_label)

        actions = QtWidgets.QHBoxLayout()
        for label, callback in (
            ("New", self.create_project),
            ("Open", self.open_project),
            ("Save", self.save_project),
        ):
            button = QtWidgets.QPushButton(label)
            button.setObjectName(f"Project{label}Button")
            button.clicked.connect(callback)
            actions.addWidget(button)
        root.addLayout(actions)

        root.addWidget(self._section_heading("WORKFLOW"))
        self.workflow = QtWidgets.QListWidget()
        self.workflow.setObjectName("WorkflowSteps")
        self.workflow.setSelectionMode(QtWidgets.QAbstractItemView.SelectionMode.SingleSelection)
        for stage in ("Requirements", "Design", "Configure physics", "Run studies", "Inspect", "Compare", "Report"):
            self.workflow.addItem(stage)
        self.workflow.setCurrentRow(0)
        root.addWidget(self.workflow, 1)

        root.addWidget(self._section_heading("MECHANISM"))
        self.model_tree = QtWidgets.QTreeWidget()
        self.model_tree.setObjectName("MechanismTree")
        self.model_tree.setHeaderHidden(True)
        for label in ("Bodies", "Interfaces & joints", "Actuators", "Contact pairs", "Materials", "Requirements"):
            item = QtWidgets.QTreeWidgetItem([label])
            item.setExpanded(True)
            self.model_tree.addTopLevelItem(item)
        root.addWidget(self.model_tree, 2)

        self.workflow.currentItemChanged.connect(self._workflow_changed)
        self.refresh()

    @staticmethod
    def _section_heading(text: str) -> QtWidgets.QLabel:
        label = QtWidgets.QLabel(text)
        label.setObjectName("SectionHeading")
        return label

    def _workflow_changed(self, current, previous=None) -> None:
        stage = current.text() if current is not None else ""
        if stage:
            QtWidgets.QToolTip.showText(QtGui.QCursor.pos(), f"{stage} workspace")

    def create_project(self) -> None:
        name, accepted = QtWidgets.QInputDialog.getText(
            self, "New DatumLoop DS Project", "Project name:", text="Untitled Mechanism"
        )
        if not accepted:
            return
        try:
            project_actions.create_project(name)
        except Exception as exc:  # surfaced in the workbench instead of a silent console failure
            QtWidgets.QMessageBox.critical(self, "Could not create project", str(exc))
        self.refresh()

    def open_project(self) -> None:
        try:
            project_actions.open_project(self)
        except Exception as exc:
            QtWidgets.QMessageBox.critical(self, "Could not open project", str(exc))
        self.refresh()

    def save_project(self) -> None:
        try:
            project_actions.save_project(self)
        except Exception as exc:
            QtWidgets.QMessageBox.critical(self, "Could not save project", str(exc))
        self.refresh()

    def refresh(self) -> None:
        doc = App.ActiveDocument
        self.project_label.setText(doc.Label if doc is not None else "No project open")


class EngineeringPanel(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        root = QtWidgets.QVBoxLayout(self)
        root.setContentsMargins(10, 10, 10, 10)

        self.tabs = QtWidgets.QTabWidget()
        self.tabs.setObjectName("EngineeringTabs")
        self.properties = QtWidgets.QWidget()
        property_layout = QtWidgets.QVBoxLayout(self.properties)
        property_layout.setContentsMargins(8, 12, 8, 8)
        self.selection_label = QtWidgets.QLabel("Select a model object to inspect its properties.")
        self.selection_label.setObjectName("SelectionSummary")
        self.selection_label.setWordWrap(True)
        property_layout.addWidget(self.selection_label)
        property_layout.addStretch(1)

        self.context = QtWidgets.QWidget()
        context_layout = QtWidgets.QVBoxLayout(self.context)
        context_layout.setContentsMargins(8, 12, 8, 8)
        self.context_copy = QtWidgets.QLabel(
            "Engineering guidance will appear here as requirements and studies are added."
        )
        self.context_copy.setWordWrap(True)
        context_layout.addWidget(self.context_copy)
        context_layout.addStretch(1)

        self.tabs.addTab(self.properties, "Properties")
        self.tabs.addTab(self.context, "Agent")
        root.addWidget(self.tabs)

    def set_workflow_stage(self, current, previous=None) -> None:
        stage = current.text() if current is not None else "Requirements"
        self.context_copy.setText(
            f"{stage}

Engineering guidance will appear here as requirements and studies are added."
        )

    def refresh(self) -> None:
        selected = Gui.Selection.getSelection()
        if not selected:
            self.selection_label.setText("Select a model object to inspect its properties.")
            return
        obj = selected[0]
        object_type = getattr(obj, "TypeId", "FreeCAD object")
        self.selection_label.setText(f"<b>{obj.Label}</b><br><span style='color:#94a3b8'>{object_type}</span>")


class StudyWorkspace(QtWidgets.QWidget):
    EMPTY_STATES = {
        "Jobs": "No studies are running.",
        "Playback": "Motion playback will be available when a study produces results.",
        "Plots": "Study plots will appear here.",
        "Evidence": "Requirement evidence will be linked to study results here.",
    }

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(8, 8, 8, 8)
        self.tabs = QtWidgets.QTabWidget()
        self.tabs.setObjectName("StudyTabs")
        for title, message in self.EMPTY_STATES.items():
            view = QtWidgets.QLabel(message)
            view.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
            view.setObjectName(f"{title}EmptyState")
            self.tabs.addTab(view, title)
        layout.addWidget(self.tabs)


def _get_or_create_dock(main_window, title, object_name, widget_factory, area):
    dock = main_window.findChild(QtWidgets.QDockWidget, object_name)
    if dock is None:
        dock = QtWidgets.QDockWidget(title, main_window)
        dock.setObjectName(object_name)
        dock.setWidget(widget_factory(main_window))
        dock.setFeatures(
            QtWidgets.QDockWidget.DockWidgetFeature.DockWidgetMovable
            | QtWidgets.QDockWidget.DockWidgetFeature.DockWidgetFloatable
        )
        main_window.addDockWidget(area, dock)
    dock.show()
    return dock


def install_shell():
    """Create the DatumLoop panels while preserving FreeCAD's 3D central view."""

    main_window = Gui.getMainWindow()
    if main_window is None:
        raise RuntimeError("FreeCAD main window is not available")

    app = QtWidgets.QApplication.instance()
    if app is not None:
        app.setApplicationName(APP_TITLE)
        app.setApplicationDisplayName(APP_TITLE)

    icon = icon_path()
    if icon.is_file():
        main_window.setWindowIcon(QtGui.QIcon(str(icon)))
    main_window.setWindowTitle(APP_TITLE)

    left = QtCore.Qt.DockWidgetArea.LeftDockWidgetArea
    right = QtCore.Qt.DockWidgetArea.RightDockWidgetArea
    bottom = QtCore.Qt.DockWidgetArea.BottomDockWidgetArea
    navigator = _get_or_create_dock(
        main_window,
        "Project Navigator",
        DOCK_OBJECT_NAMES[0],
        ProjectNavigator,
        left,
    )
    engineering = _get_or_create_dock(
        main_window,
        "Engineering",
        DOCK_OBJECT_NAMES[1],
        EngineeringPanel,
        right,
    )
    study = _get_or_create_dock(
        main_window,
        "Studies",
        DOCK_OBJECT_NAMES[2],
        StudyWorkspace,
        bottom,
    )

    workflow = navigator.widget().workflow
    if not workflow.property("DatumLoopDS.ContextLinked"):
        workflow.currentItemChanged.connect(engineering.widget().set_workflow_stage)
        workflow.setProperty("DatumLoopDS.ContextLinked", True)
    engineering.widget().set_workflow_stage(workflow.currentItem())

    timer = main_window.findChild(QtCore.QTimer, "DatumLoopDS.RefreshTimer")
    if timer is None:
        timer = QtCore.QTimer(main_window)
        timer.setObjectName("DatumLoopDS.RefreshTimer")
        timer.setInterval(750)
        timer.timeout.connect(navigator.widget().refresh)
        timer.timeout.connect(engineering.widget().refresh)
        timer.start()

    main_window.resizeDocks([navigator, engineering], [250, 300], QtCore.Qt.Orientation.Horizontal)
    main_window.resizeDocks([study], [190], QtCore.Qt.Orientation.Vertical)
    return main_window
