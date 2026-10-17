"""The backend-neutral exact-geometry contract and common adapter errors."""

from __future__ import annotations

from pathlib import Path
from typing import Protocol, Sequence

from .capabilities import KernelCapabilities
from .types import Axis3D, BoundingBox, GeometryShape, Point3, TopologyCounts, TriangleMesh, Vector3


class GeometryOperationError(RuntimeError):
    """A supported kernel operation failed for the supplied geometry."""

    def __init__(self, kernel: str, operation: str, detail: str):
        self.kernel = kernel
        self.operation = operation
        self.detail = detail
        super().__init__(f"{kernel} {operation} failed: {detail}")


class UnsupportedKernelOperation(GeometryOperationError):
    """The selected backend does not implement an exact operation."""

    def __init__(self, kernel: str, operation: str):
        super().__init__(kernel, operation, "operation is not supported by this backend")


class KernelUnavailableError(RuntimeError):
    """A backend's host runtime or optional package is not installed."""


class GeometryKernel(Protocol):
    """Common exact-geometry operations used by DatumLoop engineering services."""

    name: str
    capabilities: KernelCapabilities

    def box(self, length: float, width: float, height: float, *, center: Point3 = (0, 0, 0)) -> GeometryShape: ...
    def cylinder(self, radius: float, height: float, *, center: Point3 = (0, 0, 0)) -> GeometryShape: ...
    def sphere(self, radius: float, *, center: Point3 = (0, 0, 0)) -> GeometryShape: ...
    def cone(self, bottom_radius: float, top_radius: float, height: float, *, center: Point3 = (0, 0, 0)) -> GeometryShape: ...
    def extrude(self, profile: GeometryShape, direction: Vector3, distance: float) -> GeometryShape: ...
    def revolve(self, profile: GeometryShape, axis: Axis3D, angle_degrees: float = 360.0) -> GeometryShape: ...
    def sweep(self, profile: GeometryShape, path: GeometryShape) -> GeometryShape: ...
    def loft(self, sections: Sequence[GeometryShape], *, ruled: bool = False) -> GeometryShape: ...
    def union(self, base: GeometryShape, *others: GeometryShape) -> GeometryShape: ...
    def cut(self, base: GeometryShape, *tools: GeometryShape) -> GeometryShape: ...
    def intersection(self, left: GeometryShape, right: GeometryShape) -> GeometryShape: ...
    def fillet(self, shape: GeometryShape, radius: float, edge_indices: Sequence[int] | None = None) -> GeometryShape: ...
    def chamfer(self, shape: GeometryShape, length: float, edge_indices: Sequence[int] | None = None) -> GeometryShape: ...
    def shell(self, shape: GeometryShape, thickness: float, face_indices: Sequence[int] | None = None) -> GeometryShape: ...
    def offset(self, shape: GeometryShape, distance: float) -> GeometryShape: ...
    def bounding_box(self, shape: GeometryShape) -> BoundingBox: ...
    def volume(self, shape: GeometryShape) -> float: ...
    def surface_area(self, shape: GeometryShape) -> float: ...
    def center_of_mass(self, shape: GeometryShape) -> Point3: ...
    def topology_counts(self, shape: GeometryShape) -> TopologyCounts: ...
    def is_valid(self, shape: GeometryShape) -> bool: ...
    def tessellate(self, shape: GeometryShape, *, tolerance: float | None = None, angle_degrees: float = 15.0) -> TriangleMesh: ...
    def import_step(self, path: str | Path) -> tuple[GeometryShape, ...]: ...
    def export_step(self, shapes: Sequence[GeometryShape], path: str | Path) -> None: ...
    def serialize_brep(self, shape: GeometryShape) -> bytes: ...
    def deserialize_brep(self, data: bytes) -> GeometryShape: ...
    def export_usd_brep(self, shapes: Sequence[GeometryShape], path: str | Path) -> None: ...
    def import_usd_brep(self, path: str | Path) -> tuple[GeometryShape, ...]: ...


class GeometryKernelAdapter:
    """Shared ownership checks, error wrapping, and capability enforcement."""

    name = "unknown"

    def __init__(self) -> None:
        self._identity = object()
        self.capabilities = KernelCapabilities()

    def _wrap(self, native: object) -> GeometryShape:
        if native is None:
            raise GeometryOperationError(self.name, "wrap", "the kernel returned no shape")
        return GeometryShape(self.name, self._identity, native)

    def _unwrap(self, shape: GeometryShape) -> object:
        if not isinstance(shape, GeometryShape):
            raise TypeError("expected a GeometryShape; wrap the native object with its originating backend")
        if shape._owner is not self._identity:
            raise ValueError(f"shape belongs to {shape.backend_name}, not this {self.name} backend instance")
        return shape._native

    def _require(self, capability: str) -> None:
        if not getattr(self.capabilities, capability, False):
            raise UnsupportedKernelOperation(self.name, capability)

    def _call(self, operation: str, function):
        try:
            return function()
        except (UnsupportedKernelOperation, GeometryOperationError):
            raise
        except Exception as exc:
            raise GeometryOperationError(self.name, operation, str(exc)) from exc

    def export_usd_brep(self, shapes: Sequence[GeometryShape], path: str | Path) -> None:
        self._require("usd_brep")
        raise UnsupportedKernelOperation(self.name, "usd_brep")

    def import_usd_brep(self, path: str | Path) -> tuple[GeometryShape, ...]:
        self._require("usd_brep")
        raise UnsupportedKernelOperation(self.name, "usd_brep")
