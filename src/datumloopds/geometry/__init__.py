"""Exact-geometry backend contracts and adapters.

The FreeCAD/OCCT adapter is the production default. SMLib is an optional second
backend and is imported only when requested.
"""

from __future__ import annotations

from .capabilities import KernelCapabilities
from .conversion import convert_shape
from .kernel import (
    GeometryKernel,
    GeometryOperationError,
    KernelUnavailableError,
    UnsupportedKernelOperation,
)
from .types import Axis3D, BoundingBox, GeometryShape, TopologyCounts, TriangleMesh


def create_kernel(name: str = "occt") -> GeometryKernel:
    """Construct a backend on demand without importing optional kernels globally."""

    backend = name.strip().lower()
    if backend in {"occt", "freecad", "occ"}:
        from .occt import OCCTBackend

        return OCCTBackend()
    if backend in {"smlib", "smlibpy"}:
        from .smlib import SMLibBackend

        return SMLibBackend()
    raise ValueError(f"unknown geometry backend {name!r}; choose 'occt' or 'smlib'")


__all__ = [
    "Axis3D",
    "BoundingBox",
    "GeometryKernel",
    "GeometryOperationError",
    "GeometryShape",
    "KernelCapabilities",
    "KernelUnavailableError",
    "SMLibBackend",
    "TopologyCounts",
    "TriangleMesh",
    "UnsupportedKernelOperation",
    "OCCTBackend",
    "convert_shape",
    "create_kernel",
]


def __getattr__(name: str):
    if name == "OCCTBackend":
        from .occt import OCCTBackend

        return OCCTBackend
    if name == "SMLibBackend":
        from .smlib import SMLibBackend

        return SMLibBackend
    raise AttributeError(name)
