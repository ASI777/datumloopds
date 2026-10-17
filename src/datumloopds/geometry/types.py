"""Portable value types for exact-geometry kernel adapters.

Kernel-native objects are held by :class:`GeometryShape` and stay inside their
own adapter. Coordinates and analysis results crossing the adapter boundary use
small immutable Python values instead.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any, Iterable


Vector3 = tuple[float, float, float]
Point3 = tuple[float, float, float]
Triangle = tuple[int, int, int]


def vector3(value: Iterable[float], *, label: str = "vector") -> Vector3:
    """Return a finite three-coordinate tuple."""

    try:
        result = tuple(float(component) for component in value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{label} must contain three finite numbers") from exc
    if len(result) != 3 or not all(math.isfinite(component) for component in result):
        raise ValueError(f"{label} must contain three finite numbers")
    return result  # type: ignore[return-value]


@dataclass(frozen=True, slots=True)
class Axis3D:
    """A world-space axis with a normalized direction."""

    origin: Point3 = (0.0, 0.0, 0.0)
    direction: Vector3 = (0.0, 0.0, 1.0)

    def __post_init__(self) -> None:
        origin = vector3(self.origin, label="axis origin")
        direction = vector3(self.direction, label="axis direction")
        length = math.sqrt(sum(component * component for component in direction))
        if length <= 1e-15:
            raise ValueError("axis direction must not be zero")
        object.__setattr__(self, "origin", origin)
        object.__setattr__(self, "direction", tuple(c / length for c in direction))


@dataclass(frozen=True, slots=True)
class BoundingBox:
    """Axis-aligned bounds in the backend's model units (millimetres in FreeCAD)."""

    minimum: Point3
    maximum: Point3

    def __post_init__(self) -> None:
        minimum = vector3(self.minimum, label="bounding-box minimum")
        maximum = vector3(self.maximum, label="bounding-box maximum")
        if any(lo > hi for lo, hi in zip(minimum, maximum)):
            raise ValueError("bounding-box minimum must not exceed maximum")
        object.__setattr__(self, "minimum", minimum)
        object.__setattr__(self, "maximum", maximum)

    @property
    def size(self) -> Vector3:
        return tuple(hi - lo for lo, hi in zip(self.minimum, self.maximum))  # type: ignore[return-value]

    @property
    def center(self) -> Point3:
        return tuple((lo + hi) * 0.5 for lo, hi in zip(self.minimum, self.maximum))  # type: ignore[return-value]


@dataclass(frozen=True, slots=True)
class TriangleMesh:
    """Display or analysis tessellation of an exact shape."""

    vertices: tuple[Point3, ...]
    triangles: tuple[Triangle, ...]
    normals: tuple[tuple[Vector3, Vector3, Vector3], ...] | None = None

    def __post_init__(self) -> None:
        vertices = tuple(vector3(point, label="mesh vertex") for point in self.vertices)
        triangles = tuple(tuple(int(index) for index in tri) for tri in self.triangles)
        if any(len(triangle) != 3 for triangle in triangles):
            raise ValueError("mesh faces must be triangles")
        if any(index < 0 or index >= len(vertices) for tri in triangles for index in tri):
            raise ValueError("mesh triangle index is outside the vertex array")
        normals = self.normals
        if normals is not None:
            normals = tuple(
                tuple(vector3(normal, label="mesh normal") for normal in tri)  # type: ignore[arg-type]
                for tri in normals
            )
            if len(normals) != len(triangles) or any(len(tri) != 3 for tri in normals):
                raise ValueError("mesh normals must have one three-normal row per triangle")
        object.__setattr__(self, "vertices", vertices)
        object.__setattr__(self, "triangles", triangles)
        object.__setattr__(self, "normals", normals)

    @property
    def triangle_count(self) -> int:
        return len(self.triangles)


@dataclass(frozen=True, slots=True)
class TopologyCounts:
    """Counts of exact topological entities in a backend-native shape."""

    solids: int
    faces: int
    edges: int
    vertices: int


@dataclass(frozen=True, eq=False, slots=True)
class GeometryShape:
    """An exact shape owned by one specific backend instance."""

    backend_name: str
    _owner: object = field(repr=False)
    _native: Any = field(repr=False)

    def __repr__(self) -> str:
        return f"GeometryShape(backend={self.backend_name!r})"
