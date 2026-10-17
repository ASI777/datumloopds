"""Capability declarations shared by the exact-geometry backends."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class KernelCapabilities:
    """Operations a backend can perform without changing geometry representation."""

    primitive_creation: bool = False
    box: bool = False
    cylinder: bool = False
    sphere: bool = False
    cone: bool = False
    extrude: bool = False
    revolve: bool = False
    sweep: bool = False
    loft: bool = False
    boolean_union: bool = False
    boolean_cut: bool = False
    boolean_intersection: bool = False
    fillet: bool = False
    chamfer: bool = False
    shell: bool = False
    offset: bool = False
    bounding_box: bool = False
    volume: bool = False
    surface_area: bool = False
    center_of_mass: bool = False
    topology_counts: bool = False
    validity: bool = False
    tessellation: bool = False
    step_import: bool = False
    step_export: bool = False
    brep_serialization: bool = False
    usd_brep: bool = False
