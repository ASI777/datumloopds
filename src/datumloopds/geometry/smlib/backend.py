"""Adapter for the public smlibpy 0.3.4 API, backed by NVIDIA SMLib."""

from __future__ import annotations

import tempfile
from pathlib import Path
from typing import Sequence

from ..capabilities import KernelCapabilities
from ..kernel import GeometryKernelAdapter, KernelUnavailableError
from ..types import Axis3D, BoundingBox, GeometryShape, Point3, TopologyCounts, TriangleMesh, Vector3, vector3


def _tuple3(value: object, *, label: str) -> Vector3:
    to_tuple = getattr(value, "to_tuple", None)
    if callable(to_tuple):
        return vector3(to_tuple(), label=label)
    return vector3((getattr(value, axis) for axis in ("x", "y", "z")), label=label)


class SMLibBackend(GeometryKernelAdapter):
    """Use smlibpy's exact SMLib B-Rep operations without OCCT fallbacks.

    The wrapped smlibpy shape object retains its ``BrepHandle``. That handle
    owns the C++ Brep and releases it when the last Python reference disappears.
    Keeping the native object in :class:`GeometryShape` therefore preserves the
    upstream lifetime model instead of copying or borrowing a raw pointer.
    """

    name = "smlib"

    def __init__(self, *, smlib_module=None):
        super().__init__()
        if smlib_module is None:
            try:
                import smlibpy as smlib_module
            except ImportError as exc:
                raise KernelUnavailableError(
                    "SMLibBackend requires the optional smlibpy 0.3.4 runtime; install datumloopds[smlib]"
                ) from exc
        self._smlib = smlib_module
        part = getattr(smlib_module, "Part", None)
        sketch = getattr(smlib_module, "Sketch", None)
        if part is None:
            raise KernelUnavailableError("the installed smlibpy package does not expose smlibpy.Part")
        has = lambda obj, attr: callable(getattr(obj, attr, None))
        has_value = lambda obj, attr: hasattr(obj, attr)
        primitive_symbols = ("Box", "Cylinder", "Sphere", "Cone")
        self.capabilities = KernelCapabilities(
            primitive_creation=all(has(smlib_module, name) for name in primitive_symbols),
            box=has(smlib_module, "Box"),
            cylinder=has(smlib_module, "Cylinder"),
            sphere=has(smlib_module, "Sphere"),
            cone=has(smlib_module, "Cone"),
            extrude=has(smlib_module, "extrude") and has(sketch, "extrude"),
            revolve=has(smlib_module, "revolve") and has(sketch, "revolve"),
            sweep=has(smlib_module, "sweep"),
            loft=has(smlib_module, "loft"),
            boolean_union=has(part, "fuse"),
            boolean_cut=has(part, "cut"),
            boolean_intersection=has(part, "intersect"),
            fillet=has(part, "fillet"),
            chamfer=has(part, "chamfer"),
            shell=has(part, "shell"),
            offset=has(part, "offset"),
            bounding_box=has(part, "bbox"),
            volume=has_value(part, "volume"),
            surface_area=has_value(part, "area"),
            center_of_mass=has(part, "center"),
            topology_counts=has(part, "solids") and has(part, "faces") and has(part, "edges")
            and has(part, "vertices"),
            validity=has(part, "is_valid"),
            tessellation=has(smlib_module, "tessellate"),
            step_import=has(smlib_module, "import_step"),
            step_export=has(smlib_module, "export_step"),
            brep_serialization=has(smlib_module, "save") and has(smlib_module, "load"),
            usd_brep=has(smlib_module, "export_usd") and has(smlib_module, "import_usd"),
        )

    def wrap_native(self, shape: object) -> GeometryShape:
        """Wrap a shape created by this smlibpy module (for example a Sketch)."""

        shape_type = getattr(self._smlib, "Shape", None)
        if shape_type is not None and not isinstance(shape, shape_type):
            raise TypeError("wrap_native expects a shape created by this smlibpy module")
        return self._wrap(shape)

    def _edges(self, shape, indices: Sequence[int] | None):
        edges = list(shape.edges())
        selected = range(len(edges)) if indices is None else indices
        try:
            return [edges[index] for index in selected]
        except IndexError as exc:
            raise ValueError("edge index is outside the shape's edge array") from exc

    def _faces(self, shape, indices: Sequence[int] | None):
        faces = list(shape.faces())
        selected = () if indices is None else indices
        try:
            return [faces[index] for index in selected]
        except IndexError as exc:
            raise ValueError("face index is outside the shape's face array") from exc

    def box(self, length: float, width: float, height: float, *, center: Point3 = (0, 0, 0)) -> GeometryShape:
        self._require("box")
        def build():
            native = self._smlib.Box(float(length), float(width), float(height))
            return native.translate(vector3(center, label="box center"))
        return self._wrap(self._call("box", build))

    def cylinder(self, radius: float, height: float, *, center: Point3 = (0, 0, 0)) -> GeometryShape:
        self._require("cylinder")
        def build():
            native = self._smlib.Cylinder(float(radius), float(height))
            return native.translate(vector3(center, label="cylinder center"))
        return self._wrap(self._call("cylinder", build))

    def sphere(self, radius: float, *, center: Point3 = (0, 0, 0)) -> GeometryShape:
        self._require("sphere")
        def build():
            native = self._smlib.Sphere(float(radius))
            return native.translate(vector3(center, label="sphere center"))
        return self._wrap(self._call("sphere", build))

    def cone(self, bottom_radius: float, top_radius: float, height: float,
             *, center: Point3 = (0, 0, 0)) -> GeometryShape:
        self._require("cone")
        def build():
            native = self._smlib.Cone(float(bottom_radius), float(top_radius), float(height))
            return native.translate(vector3(center, label="cone center"))
        return self._wrap(self._call("cone", build))

    def extrude(self, profile: GeometryShape, direction: Vector3, distance: float) -> GeometryShape:
        self._require("extrude")
        def build():
            unit = Axis3D(direction=direction).direction
            return self._unwrap(profile).extrude(float(distance), direction=unit)
        return self._wrap(self._call("extrude", build))

    def revolve(self, profile: GeometryShape, axis: Axis3D, angle_degrees: float = 360.0) -> GeometryShape:
        self._require("revolve")
        def build():
            native_axis = self._smlib.Axis(axis.origin, axis.direction)
            return self._unwrap(profile).revolve(native_axis, float(angle_degrees))
        return self._wrap(self._call("revolve", build))

    def sweep(self, profile: GeometryShape, path: GeometryShape) -> GeometryShape:
        self._require("sweep")
        return self._wrap(self._call("sweep", lambda: self._smlib.sweep(self._unwrap(profile), self._unwrap(path))))

    def loft(self, sections: Sequence[GeometryShape], *, ruled: bool = False) -> GeometryShape:
        self._require("loft")
        def build():
            return self._smlib.loft([self._unwrap(section) for section in sections], ruled=bool(ruled))
        return self._wrap(self._call("loft", build))

    def union(self, base: GeometryShape, *others: GeometryShape) -> GeometryShape:
        self._require("boolean_union")
        return self._wrap(self._call("union", lambda: self._unwrap(base).fuse(*(self._unwrap(item) for item in others))))

    def cut(self, base: GeometryShape, *tools: GeometryShape) -> GeometryShape:
        self._require("boolean_cut")
        return self._wrap(self._call("cut", lambda: self._unwrap(base).cut(*(self._unwrap(item) for item in tools))))

    def intersection(self, left: GeometryShape, right: GeometryShape) -> GeometryShape:
        self._require("boolean_intersection")
        return self._wrap(self._call("intersection", lambda: self._unwrap(left).intersect(self._unwrap(right))))

    def fillet(self, shape: GeometryShape, radius: float, edge_indices: Sequence[int] | None = None) -> GeometryShape:
        self._require("fillet")
        def build():
            native = self._unwrap(shape)
            return native.fillet(self._edges(native, edge_indices), float(radius))
        return self._wrap(self._call("fillet", build))

    def chamfer(self, shape: GeometryShape, length: float, edge_indices: Sequence[int] | None = None) -> GeometryShape:
        self._require("chamfer")
        def build():
            native = self._unwrap(shape)
            return native.chamfer(self._edges(native, edge_indices), float(length))
        return self._wrap(self._call("chamfer", build))

    def shell(self, shape: GeometryShape, thickness: float, face_indices: Sequence[int] | None = None) -> GeometryShape:
        self._require("shell")
        def build():
            native = self._unwrap(shape)
            return native.shell(float(thickness), self._faces(native, face_indices))
        return self._wrap(self._call("shell", build))

    def offset(self, shape: GeometryShape, distance: float) -> GeometryShape:
        self._require("offset")
        return self._wrap(self._call("offset", lambda: self._unwrap(shape).offset(float(distance))))

    def bounding_box(self, shape: GeometryShape) -> BoundingBox:
        self._require("bounding_box")
        def build():
            bounds = self._unwrap(shape).bbox()
            return BoundingBox(_tuple3(bounds.min, label="bounding-box minimum"),
                               _tuple3(bounds.max, label="bounding-box maximum"))
        return self._call("bounding_box", build)

    def volume(self, shape: GeometryShape) -> float:
        self._require("volume")
        return self._call("volume", lambda: float(self._unwrap(shape).volume))

    def surface_area(self, shape: GeometryShape) -> float:
        self._require("surface_area")
        return self._call("surface_area", lambda: float(self._unwrap(shape).area))

    def center_of_mass(self, shape: GeometryShape) -> Point3:
        self._require("center_of_mass")
        return self._call("center_of_mass", lambda: _tuple3(self._unwrap(shape).center(), label="center of mass"))

    def is_valid(self, shape: GeometryShape) -> bool:
        self._require("validity")
        return bool(self._call("is_valid", lambda: self._unwrap(shape).is_valid()))

    def topology_counts(self, shape: GeometryShape) -> TopologyCounts:
        self._require("topology_counts")
        def build():
            native = self._unwrap(shape)
            return TopologyCounts(
                len(native.solids()), len(native.faces()), len(native.edges()), len(native.vertices())
            )
        return self._call("topology_counts", build)

    def tessellate(self, shape: GeometryShape, *, tolerance: float | None = None,
                   angle_degrees: float = 15.0) -> TriangleMesh:
        self._require("tessellation")
        def build():
            mesh = self._smlib.tessellate(self._unwrap(shape), tolerance=tolerance, angle=float(angle_degrees))
            vertices = tuple(vector3(vertex, label="mesh vertex") for vertex in mesh.vertices)
            triangles = tuple(tuple(int(index) for index in tri) for tri in mesh.triangles)
            normals = None
            if mesh.normals is not None:
                normals = tuple(
                    tuple(vector3(normal, label="mesh normal") for normal in tri)
                    for tri in mesh.normals
                )
            return TriangleMesh(vertices, triangles, normals)
        return self._call("tessellate", build)

    def import_step(self, path: str | Path) -> tuple[GeometryShape, ...]:
        self._require("step_import")
        imported = self._call("step_import", lambda: self._smlib.import_step(str(path)))
        shape_list = getattr(self._smlib, "ShapeList", ())
        if shape_list and isinstance(imported, shape_list):
            native_shapes = tuple(imported)
        elif isinstance(imported, (list, tuple)):
            native_shapes = tuple(imported)
        else:
            native_shapes = (imported,)
        return tuple(self._wrap(shape) for shape in native_shapes)

    def export_step(self, shapes: Sequence[GeometryShape], path: str | Path) -> None:
        self._require("step_export")
        if not shapes:
            raise ValueError("STEP export needs at least one shape")
        natives = [self._unwrap(shape) for shape in shapes]
        self._call("step_export", lambda: self._smlib.export_step(natives, str(path), unit="mm"))

    def serialize_brep(self, shape: GeometryShape) -> bytes:
        self._require("brep_serialization")
        def build():
            with tempfile.TemporaryDirectory(prefix="datumloopds-smlib-brep-") as temp_dir:
                path = Path(temp_dir) / "shape.smb"
                self._smlib.save(self._unwrap(shape), path, ascii=False)
                return path.read_bytes()
        return self._call("serialize_brep", build)

    def deserialize_brep(self, data: bytes) -> GeometryShape:
        self._require("brep_serialization")
        def build():
            with tempfile.TemporaryDirectory(prefix="datumloopds-smlib-brep-") as temp_dir:
                path = Path(temp_dir) / "shape.smb"
                path.write_bytes(data)
                return self._smlib.load(path)
        return self._wrap(self._call("deserialize_brep", build))

    def export_usd_brep(self, shapes: Sequence[GeometryShape], path: str | Path) -> None:
        self._require("usd_brep")
        if not shapes:
            raise ValueError("USD B-Rep export needs at least one shape")
        natives = [self._unwrap(shape) for shape in shapes]
        self._call("export_usd_brep", lambda: self._smlib.export_usd(natives, str(path), mesh=False))

    def import_usd_brep(self, path: str | Path) -> tuple[GeometryShape, ...]:
        self._require("usd_brep")
        imported = self._call("import_usd_brep", lambda: self._smlib.import_usd(str(path)))
        shape_list = getattr(self._smlib, "ShapeList", ())
        if shape_list and isinstance(imported, shape_list):
            native_shapes = tuple(imported)
        elif isinstance(imported, (list, tuple)):
            native_shapes = tuple(imported)
        else:
            native_shapes = (imported,)
        return tuple(self._wrap(shape) for shape in native_shapes)
