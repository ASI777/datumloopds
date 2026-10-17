"""Reference exact-geometry adapter backed by FreeCAD's Part module."""

from __future__ import annotations

from pathlib import Path
from typing import Sequence

from ..capabilities import KernelCapabilities
from ..kernel import GeometryKernelAdapter, KernelUnavailableError
from ..types import Axis3D, BoundingBox, GeometryShape, Point3, TopologyCounts, TriangleMesh, Vector3, vector3


def _point(value: object) -> Point3:
    return vector3((getattr(value, axis) for axis in ("x", "y", "z")), label="point")


class OCCTBackend(GeometryKernelAdapter):
    """Use FreeCAD's native Part/TopoShape API for production CAD operations."""

    name = "occt"

    def __init__(self, *, app_module=None, part_module=None):
        super().__init__()
        if app_module is None or part_module is None:
            try:
                import FreeCAD as app_module
                import Part as part_module
            except ImportError as exc:
                raise KernelUnavailableError("OCCTBackend requires the FreeCAD Python runtime") from exc
        self._app = app_module
        self._part = part_module
        shape = part_module.makeBox(1, 1, 1)
        wire = part_module.Wire([part_module.makeLine(app_module.Vector(0, 0, 0), app_module.Vector(0, 0, 1))])
        has = lambda obj, method: callable(getattr(obj, method, None))
        has_value = lambda obj, name: hasattr(obj, name)
        box = has(part_module, "makeBox")
        cylinder = has(part_module, "makeCylinder")
        sphere = has(part_module, "makeSphere")
        cone = has(part_module, "makeCone")
        self.capabilities = KernelCapabilities(
            primitive_creation=all((box, cylinder, sphere, cone)),
            box=box,
            cylinder=cylinder,
            sphere=sphere,
            cone=cone,
            extrude=has(shape, "extrude"),
            revolve=has(shape, "revolve"),
            sweep=has(wire, "makePipeShell"),
            loft=has(part_module, "makeLoft"),
            boolean_union=has(shape, "fuse"),
            boolean_cut=has(shape, "cut"),
            boolean_intersection=has(shape, "common"),
            fillet=has(shape, "makeFillet"),
            chamfer=has(shape, "makeChamfer"),
            shell=has(shape, "makeThickness"),
            offset=has(shape, "makeOffsetShape"),
            bounding_box=has_value(shape, "BoundBox"),
            volume=has_value(shape, "Volume"),
            surface_area=has_value(shape, "Area"),
            center_of_mass=has_value(shape, "CenterOfMass"),
            topology_counts=has_value(shape, "Solids") and has_value(shape, "Faces")
            and has_value(shape, "Edges") and has_value(shape, "Vertexes"),
            validity=has(shape, "isValid"),
            tessellation=has(shape, "tessellate"),
            step_import=has(part_module, "read"),
            step_export=has(shape, "exportStep"),
            brep_serialization=has(shape, "exportBrepToString") and has(shape, "importBrepFromString"),
            usd_brep=False,
        )

    def wrap_native(self, shape: object) -> GeometryShape:
        """Wrap a FreeCAD Part shape (for example a face used as an extrusion profile)."""

        return self._wrap(shape)

    def _vector(self, value: Vector3):
        return self._app.Vector(*value)

    def _edges(self, shape, indices: Sequence[int] | None):
        edges = list(shape.Edges)
        selected = range(len(edges)) if indices is None else indices
        try:
            return [edges[index] for index in selected]
        except IndexError as exc:
            raise ValueError("edge index is outside the shape's edge array") from exc

    def _faces(self, shape, indices: Sequence[int] | None):
        faces = list(shape.Faces)
        selected = range(len(faces)) if indices is None else indices
        try:
            return [faces[index] for index in selected]
        except IndexError as exc:
            raise ValueError("face index is outside the shape's face array") from exc

    def box(self, length: float, width: float, height: float, *, center: Point3 = (0, 0, 0)) -> GeometryShape:
        self._require("box")
        def build():
            cx, cy, cz = vector3(center, label="box center")
            return self._part.makeBox(float(length), float(width), float(height),
                                      self._vector((cx - length / 2, cy - width / 2, cz - height / 2)))
        return self._wrap(self._call("box", build))

    def cylinder(self, radius: float, height: float, *, center: Point3 = (0, 0, 0)) -> GeometryShape:
        self._require("cylinder")
        def build():
            cx, cy, cz = vector3(center, label="cylinder center")
            return self._part.makeCylinder(float(radius), float(height), self._vector((cx, cy, cz - height / 2)))
        return self._wrap(self._call("cylinder", build))

    def sphere(self, radius: float, *, center: Point3 = (0, 0, 0)) -> GeometryShape:
        self._require("sphere")
        def build():
            return self._part.makeSphere(float(radius), self._vector(vector3(center, label="sphere center")))
        return self._wrap(self._call("sphere", build))

    def cone(self, bottom_radius: float, top_radius: float, height: float, *, center: Point3 = (0, 0, 0)) -> GeometryShape:
        self._require("cone")
        def build():
            cx, cy, cz = vector3(center, label="cone center")
            return self._part.makeCone(float(bottom_radius), float(top_radius), float(height),
                                       self._vector((cx, cy, cz - height / 2)))
        return self._wrap(self._call("cone", build))

    def extrude(self, profile: GeometryShape, direction: Vector3, distance: float) -> GeometryShape:
        self._require("extrude")
        def build():
            unit = Axis3D(direction=direction).direction
            vector = self._vector(tuple(component * float(distance) for component in unit))
            return self._unwrap(profile).extrude(vector)
        return self._wrap(self._call("extrude", build))

    def revolve(self, profile: GeometryShape, axis: Axis3D, angle_degrees: float = 360.0) -> GeometryShape:
        self._require("revolve")
        def build():
            return self._unwrap(profile).revolve(self._vector(axis.origin), self._vector(axis.direction),
                                                  float(angle_degrees))
        return self._wrap(self._call("revolve", build))

    def sweep(self, profile: GeometryShape, path: GeometryShape) -> GeometryShape:
        self._require("sweep")
        def build():
            native_path = self._unwrap(path)
            if not callable(getattr(native_path, "makePipeShell", None)):
                native_path = self._part.Wire(list(native_path.Edges))
            native_profile = self._unwrap(profile)
            profiles = list(getattr(native_profile, "Wires", ())) or [native_profile]
            return native_path.makePipeShell(profiles, True, False)
        return self._wrap(self._call("sweep", build))

    def loft(self, sections: Sequence[GeometryShape], *, ruled: bool = False) -> GeometryShape:
        self._require("loft")
        def build():
            native_sections = [self._unwrap(section) for section in sections]
            return self._part.makeLoft(native_sections, True, bool(ruled), False)
        return self._wrap(self._call("loft", build))

    def union(self, base: GeometryShape, *others: GeometryShape) -> GeometryShape:
        self._require("boolean_union")
        def build():
            result = self._unwrap(base)
            for other in others:
                result = result.fuse(self._unwrap(other))
            return result
        return self._wrap(self._call("union", build))

    def cut(self, base: GeometryShape, *tools: GeometryShape) -> GeometryShape:
        self._require("boolean_cut")
        def build():
            result = self._unwrap(base)
            for tool in tools:
                result = result.cut(self._unwrap(tool))
            return result
        return self._wrap(self._call("cut", build))

    def intersection(self, left: GeometryShape, right: GeometryShape) -> GeometryShape:
        self._require("boolean_intersection")
        return self._wrap(self._call("intersection", lambda: self._unwrap(left).common(self._unwrap(right))))

    def fillet(self, shape: GeometryShape, radius: float, edge_indices: Sequence[int] | None = None) -> GeometryShape:
        self._require("fillet")
        def build():
            native = self._unwrap(shape)
            return native.makeFillet(float(radius), self._edges(native, edge_indices))
        return self._wrap(self._call("fillet", build))

    def chamfer(self, shape: GeometryShape, length: float, edge_indices: Sequence[int] | None = None) -> GeometryShape:
        self._require("chamfer")
        def build():
            native = self._unwrap(shape)
            return native.makeChamfer(float(length), self._edges(native, edge_indices))
        return self._wrap(self._call("chamfer", build))

    def shell(self, shape: GeometryShape, thickness: float, face_indices: Sequence[int] | None = None) -> GeometryShape:
        self._require("shell")
        def build():
            native = self._unwrap(shape)
            tolerance = max(abs(float(thickness)) * 1e-4, 1e-6)
            faces = self._faces(native, face_indices) if face_indices is not None else []
            return native.makeThickness(faces, float(thickness), tolerance)
        return self._wrap(self._call("shell", build))

    def offset(self, shape: GeometryShape, distance: float) -> GeometryShape:
        self._require("offset")
        def build():
            tolerance = max(abs(float(distance)) * 1e-4, 1e-6)
            return self._unwrap(shape).makeOffsetShape(float(distance), tolerance)
        return self._wrap(self._call("offset", build))

    def bounding_box(self, shape: GeometryShape) -> BoundingBox:
        self._require("bounding_box")
        def build():
            native = self._unwrap(shape)
            optimal_bounds = getattr(native, "optimalBoundingBox", None)
            bounds = optimal_bounds() if callable(optimal_bounds) else native.BoundBox
            return BoundingBox((bounds.XMin, bounds.YMin, bounds.ZMin), (bounds.XMax, bounds.YMax, bounds.ZMax))
        return self._call("bounding_box", build)

    def volume(self, shape: GeometryShape) -> float:
        self._require("volume")
        return self._call("volume", lambda: float(self._unwrap(shape).Volume))

    def surface_area(self, shape: GeometryShape) -> float:
        self._require("surface_area")
        return self._call("surface_area", lambda: float(self._unwrap(shape).Area))

    def center_of_mass(self, shape: GeometryShape) -> Point3:
        self._require("center_of_mass")
        def build():
            native = self._unwrap(shape)
            center = getattr(native, "CenterOfMass", None)
            if center is not None:
                return _point(center)
            solids = list(getattr(native, "Solids", ()))
            total = sum(float(solid.Volume) for solid in solids)
            if total <= 0:
                raise ValueError("FreeCAD did not provide a center of mass for this non-solid shape")
            return tuple(
                sum(float(solid.Volume) * _point(solid.CenterOfMass)[axis] for solid in solids) / total
                for axis in range(3)
            )
        return self._call("center_of_mass", build)

    def is_valid(self, shape: GeometryShape) -> bool:
        self._require("validity")
        return bool(self._call("is_valid", lambda: self._unwrap(shape).isValid()))

    def topology_counts(self, shape: GeometryShape) -> TopologyCounts:
        self._require("topology_counts")
        def build():
            native = self._unwrap(shape)
            return TopologyCounts(len(native.Solids), len(native.Faces), len(native.Edges), len(native.Vertexes))
        return self._call("topology_counts", build)

    def tessellate(self, shape: GeometryShape, *, tolerance: float | None = None,
                   angle_degrees: float = 15.0) -> TriangleMesh:
        self._require("tessellation")
        def build():
            native = self._unwrap(shape)
            bounds = self.bounding_box(shape)
            diagonal = sum(component * component for component in bounds.size) ** 0.5
            chord = float(tolerance) if tolerance is not None else max(diagonal * 1e-3, 1e-6)
            points, facets = native.tessellate(chord)
            return TriangleMesh(tuple(_point(point) for point in points),
                                tuple(tuple(int(index) for index in facet) for facet in facets))
        return self._call("tessellate", build)

    def import_step(self, path: str | Path) -> tuple[GeometryShape, ...]:
        self._require("step_import")
        imported = self._call("step_import", lambda: self._part.read(str(path)))
        return (self._wrap(imported),)

    def export_step(self, shapes: Sequence[GeometryShape], path: str | Path) -> None:
        self._require("step_export")
        if not shapes:
            raise ValueError("STEP export needs at least one shape")
        def build():
            native_shapes = [self._unwrap(shape) for shape in shapes]
            if len(native_shapes) == 1:
                native_shapes[0].exportStep(str(path))
            else:
                self._part.export(native_shapes, str(path))
        self._call("step_export", build)

    def serialize_brep(self, shape: GeometryShape) -> bytes:
        self._require("brep_serialization")
        return self._call("serialize_brep", lambda: self._unwrap(shape).exportBrepToString().encode("utf-8"))

    def deserialize_brep(self, data: bytes) -> GeometryShape:
        self._require("brep_serialization")
        def build():
            native = self._part.Shape()
            native.importBrepFromString(data.decode("utf-8"))
            return self._wrap(native)
        return self._call("deserialize_brep", build)
