#!/usr/bin/env python3
"""Pinned optional OCC mesher worker. Numerical source geometry remains DatumLoop-owned."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import time
import resource
import gmsh


def need(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def length(a):
    return math.hypot(*a)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("request", type=Path)
    parser.add_argument("result", type=Path)
    args = parser.parse_args()
    request = json.loads(args.request.read_text())
    need(request["schema_version"] == 1, "unsupported meshing request schema")
    need(gmsh.__version__ == "4.15.2", "Gmsh worker requires pinned version 4.15.2")
    need(sys.platform == "linux", "this pinned worker build targets Linux")
    memory_mib = request["maximum_memory_mib"]
    need(isinstance(memory_mib, int) and 256 <= memory_mib <= 8192, "invalid worker memory cap")
    resource.setrlimit(resource.RLIMIT_AS, (memory_mib*1024*1024, memory_mib*1024*1024))
    brep = Path(request["brep_path"])
    need(hashlib.sha256(brep.read_bytes()).hexdigest() == request["geometry_sha256"], "exact geometry digest mismatch")
    started = time.perf_counter()
    gmsh.initialize(["gmsh", "-nopopup"])
    try:
        gmsh.option.setNumber("General.Terminal", 0)
        gmsh.option.setNumber("General.NumThreads", 1)
        gmsh.option.setNumber("Geometry.OCCBoundsUseStl", 0)
        gmsh.option.setNumber("Geometry.OCCFixSmallEdges", 0)
        gmsh.option.setNumber("Geometry.OCCFixSmallFaces", 0)
        gmsh.option.setNumber("Geometry.OCCSewFaces", 0)
        gmsh.logger.start()
        gmsh.model.add("DatumLoop exact analysis domain")
        volumes = gmsh.model.occ.importShapes(str(brep), highestDimOnly=True, format="brep")
        need(len(volumes) == 1 and volumes[0][0] == 3, "worker currently requires one exact OCC volume")
        gmsh.model.occ.synchronize()
        surfaces = gmsh.model.getEntities(2)
        need(len(surfaces) == len(request["faces"]), "OCC import changed the CAD boundary face count")
        unused = {tag for _, tag in surfaces}
        mapping = []
        for source in request["faces"]:
            matches = []
            for tag in sorted(unused):
                kind = gmsh.model.getType(2, tag).lower()
                if kind != source["surface_type"]:
                    continue
                area = gmsh.model.occ.getMass(2, tag)
                center = gmsh.model.occ.getCenterOfMass(2, tag)
                bounds = gmsh.model.getBoundingBox(2, tag)
                if (abs(area-source["area_mm2"]) <= max(1e-8, source["area_mm2"]*1e-7)
                        and max(abs(a-b) for a,b in zip(center, source["centroid_mm"])) <= 1e-4
                        and max(abs(a-b) for a,b in zip(bounds, source["bounds_mm"])) <= 1e-4):
                    matches.append(tag)
            need(len(matches) == 1, f"CAD face UUID does not map uniquely: {source['topology_reference_id']}")
            tag = matches[0]
            unused.remove(tag)
            mapping.append((source, tag))
        need(not unused, "unmatched imported CAD faces")
        # OCC BRep is in millimetres; the numerical mesh is always in SI metres.
        gmsh.model.occ.dilate(volumes, 0, 0, 0, .001, .001, .001)
        gmsh.model.occ.synchronize()
        for source, tag in mapping:
            gmsh.model.addPhysicalGroup(2, [tag], source["attribute"], source["topology_reference_id"])
        gmsh.model.addPhysicalGroup(3, [volumes[0][1]], 1, "DatumLoop material domain")
        settings = request["settings"]
        h = settings["maximum_element_size_m"]
        need(math.isfinite(h) and h > 0, "invalid maximum element size")
        need(request["exact_cad_volume_m3"] / h**3 <= settings["maximum_element_count"],
             "requested size exceeds the conservative pre-meshing resource screen")
        gmsh.option.setNumber("Mesh.MeshSizeMax", h)
        gmsh.option.setNumber("Mesh.MeshSizeMin", h*.08)
        gmsh.option.setNumber("Mesh.MeshSizeFromCurvature", 24)
        gmsh.option.setNumber("Mesh.MinimumCirclePoints", 24)
        gmsh.option.setNumber("Mesh.Algorithm3D", 1)
        gmsh.option.setNumber("Mesh.ElementOrder", 1)
        gmsh.option.setNumber("Mesh.Optimize", 1)
        gmsh.option.setNumber("Mesh.OptimizeNetgen", 1)
        gmsh.model.mesh.generate(3)
        types, tags_by_type, nodes_by_type = gmsh.model.mesh.getElements(3)
        need(list(types) == [4], "worker requires first-order tetrahedra only")
        count = len(tags_by_type[0])
        need(count <= settings["maximum_element_count"], "generated mesh exceeds requested element cap")
        tags, xyz, _ = gmsh.model.mesh.getNodes()
        node_ids = {int(tag): i for i,tag in enumerate(tags)}
        nodes = [list(map(float, xyz[3*i:3*i+3])) for i in range(len(tags))]
        tets = []
        faces = {}
        minimum_jacobian = 1.0
        maximum_aspect = 1.0
        total_volume = 0.0
        raw = nodes_by_type[0]
        for i in range(count):
            cell = [node_ids[int(x)] for x in raw[4*i:4*i+4]]
            points = [nodes[j] for j in cell]
            determinant = dot(sub(points[1], points[0]), cross(sub(points[2], points[0]), sub(points[3], points[0])))
            if determinant < 0:
                cell[1], cell[2] = cell[2], cell[1]
                points[1], points[2] = points[2], points[1]
                determinant = -determinant
            need(math.isfinite(determinant) and determinant > 0, "invalid tetrahedral Jacobian")
            total_volume += determinant / 6
            edges = [length(sub(points[a],points[b])) for a in range(4) for b in range(a+1,4)]
            maximum_aspect = max(maximum_aspect, max(edges)/min(edges))
            for vertex in range(4):
                denominator = math.prod(length(sub(points[j],points[vertex])) for j in range(4) if j != vertex)
                minimum_jacobian = min(minimum_jacobian, determinant/denominator)
                face = tuple(sorted(cell[j] for j in range(4) if j != vertex))
                entry = faces.setdefault(face, [0,cell[vertex]])
                entry[0] += 1
                need(entry[0] <= 2, "nonmanifold tetrahedral mesh")
            tets.append(cell)
        need(minimum_jacobian >= settings["minimum_scaled_jacobian"], "minimum scaled Jacobian below requested quality limit")
        need(maximum_aspect <= settings["maximum_element_aspect_ratio"], "maximum edge ratio above requested quality limit")
        boundary = []
        regions = []
        seen = set()
        maximum_area_error = 0.0
        for source, tag in mapping:
            boundary_types, _, boundary_nodes = gmsh.model.mesh.getElements(2, tag)
            need(list(boundary_types) == [2], "worker requires linear boundary triangles")
            data = boundary_nodes[0]
            area = 0.0
            for i in range(len(data)//3):
                triangle = [node_ids[int(x)] for x in data[3*i:3*i+3]]
                key = tuple(sorted(triangle))
                need(key in faces and faces[key][0] == 1 and key not in seen, "incorrect or duplicated boundary coverage")
                seen.add(key)
                a,b,c = [nodes[j] for j in triangle]
                normal = cross(sub(b,a),sub(c,a))
                if dot(normal,sub(nodes[faces[key][1]],a)) > 0:
                    triangle[1],triangle[2] = triangle[2],triangle[1]
                area += length(normal)*.5
                boundary.append([*triangle,source["attribute"]])
            cad_area = source["area_mm2"]*1e-6
            error = abs(area-cad_area)/cad_area
            maximum_area_error = max(maximum_area_error,error)
            need(error <= settings["maximum_boundary_area_relative_error"], "boundary facet area error exceeds request")
            regions.append(dict(topology_reference_id=source["topology_reference_id"], attribute=source["attribute"],
                area_m2=area, exact_cad_area_m2=cad_area, outward_normal=source["outward_normal"]))
        need(len(seen) == sum(1 for face in faces.values() if face[0] == 1), "unattributed exterior triangles")
        cad_volume = request["exact_cad_volume_m3"]
        volume_error = abs(total_volume-cad_volume)/cad_volume
        need(volume_error <= settings["maximum_volume_relative_error"], "mesh/CAD volume mismatch exceeds request")
        result = dict(schema_version=1, backend="gmsh.occ.external", version=gmsh.__version__,
            build_info=gmsh.option.getString("General.BuildInfo"), geometry_sha256=request["geometry_sha256"],
            source_project_id=request["source_project_id"], source_document_revision=request["source_document_revision"],
            cad_object_id=request["cad_object_id"], element_type="tetrahedron4", unit="m", nodes=nodes,
            tetrahedra=tets, boundary_triangles=boundary, boundary_regions=regions,
            minimum_scaled_jacobian=minimum_jacobian, maximum_aspect_ratio=maximum_aspect,
            aspect_ratio_definition="maximum edge length / minimum edge length per tetrahedron",
            jacobian_definition="minimum determinant / product of incident edge lengths at every tetrahedral vertex",
            exact_cad_volume_m3=cad_volume, mesh_volume_m3=total_volume,
            relative_volume_error=volume_error, maximum_boundary_area_relative_error=maximum_area_error,
            wall_time_s=time.perf_counter()-started, log=gmsh.logger.get())
        args.result.write_text(json.dumps(result,separators=(",",":")),encoding="utf-8")
        print(json.dumps(dict(status="passed", nodes=len(nodes), elements=count, boundary_faces=len(regions))))
    finally:
        gmsh.finalize()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Gmsh worker failed: {error}", file=sys.stderr)
        sys.exit(1)
