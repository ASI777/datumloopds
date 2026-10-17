"""Explicit exact B-Rep exchange between backends."""

from __future__ import annotations

import tempfile
from pathlib import Path

from .kernel import GeometryKernel, UnsupportedKernelOperation
from .types import GeometryShape


def convert_shape(
    shape: GeometryShape,
    source: GeometryKernel,
    target: GeometryKernel,
) -> tuple[GeometryShape, ...]:
    """Convert through STEP, preserving exact B-Rep data instead of tessellating.

    STEP is the portable interchange path between OCCT and SMLib. Conversion may
    still change tolerances, face grouping, or topology; callers should revalidate
    the target result and compare its measurements with engineering tolerances.
    """

    if not source.capabilities.step_export:
        raise UnsupportedKernelOperation(source.name, "step_export")
    if not target.capabilities.step_import:
        raise UnsupportedKernelOperation(target.name, "step_import")
    with tempfile.TemporaryDirectory(prefix="datumloopds-kernel-convert-") as temp_dir:
        step_path = Path(temp_dir) / "shape.step"
        source.export_step((shape,), step_path)
        return target.import_step(step_path)
