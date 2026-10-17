#include "datumloop/geometry/OcctKernel.hpp"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace datumloop {
namespace {

bool isPositiveFinite(double value)
{
    return std::isfinite(value) && value > 0.0;
}

std::array<float, 3> asFloat(const gp_Pnt& point)
{
    return {static_cast<float>(point.X()), static_cast<float>(point.Y()), static_cast<float>(point.Z())};
}

std::array<float, 3> asFloat(const gp_Dir& direction)
{
    return {static_cast<float>(direction.X()), static_cast<float>(direction.Y()), static_cast<float>(direction.Z())};
}

}  // namespace

TopoDS_Shape OcctKernel::box(const Vector3& dimensions)
{
    for (const double dimension : dimensions) {
        if (!isPositiveFinite(dimension)) {
            throw std::invalid_argument("box dimensions must be finite and greater than zero");
        }
    }

    const double halfX = dimensions[0] * 0.5;
    const double halfY = dimensions[1] * 0.5;
    const double halfZ = dimensions[2] * 0.5;
    return BRepPrimAPI_MakeBox(
        gp_Pnt(-halfX, -halfY, -halfZ), dimensions[0], dimensions[1], dimensions[2]).Shape();
}

ShapeMetrics OcctKernel::measure(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) {
        throw std::invalid_argument("cannot measure a null OCCT shape");
    }

    ShapeMetrics metrics;
    GProp_GProps volumeProperties;
    BRepGProp::VolumeProperties(shape, volumeProperties);
    metrics.volume = volumeProperties.Mass();
    const gp_Pnt center = volumeProperties.CentreOfMass();
    metrics.centerOfMass = {center.X(), center.Y(), center.Z()};

    GProp_GProps surfaceProperties;
    BRepGProp::SurfaceProperties(shape, surfaceProperties);
    metrics.surfaceArea = surfaceProperties.Mass();

    Bnd_Box bounds;
    BRepBndLib::AddOptimal(shape, bounds, Standard_False, Standard_False);
    Standard_Real xMin = 0.0;
    Standard_Real yMin = 0.0;
    Standard_Real zMin = 0.0;
    Standard_Real xMax = 0.0;
    Standard_Real yMax = 0.0;
    Standard_Real zMax = 0.0;
    bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
    metrics.minimum = {xMin, yMin, zMin};
    metrics.maximum = {xMax, yMax, zMax};
    metrics.valid = isValid(shape);
    return metrics;
}

bool OcctKernel::isValid(const TopoDS_Shape& shape)
{
    return !shape.IsNull() && BRepCheck_Analyzer(shape, Standard_True).IsValid();
}

TriangleMesh OcctKernel::tessellate(const TopoDS_Shape& shape,
                                    double linearDeflection,
                                    double angularDeflection)
{
    if (shape.IsNull()) {
        throw std::invalid_argument("cannot tessellate a null OCCT shape");
    }
    if (!isPositiveFinite(linearDeflection) || !isPositiveFinite(angularDeflection)) {
        throw std::invalid_argument("tessellation deflections must be finite and greater than zero");
    }

    BRepMesh_IncrementalMesh mesher(
        shape, linearDeflection, Standard_False, angularDeflection, Standard_False);
    mesher.Perform();

    TriangleMesh mesh;
    const ShapeMetrics metrics = measure(shape);
    mesh.minimum = metrics.minimum;
    mesh.maximum = metrics.maximum;

    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const TopoDS_Face face = TopoDS::Face(explorer.Current());
        TopLoc_Location location;
        const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull()) {
            continue;
        }

        const gp_Trsf& transform = location.Transformation();
        const bool reverse = face.Orientation() == TopAbs_REVERSED;
        for (Standard_Integer triangleIndex = 1;
             triangleIndex <= triangulation->NbTriangles();
             ++triangleIndex) {
            Standard_Integer node1 = 0;
            Standard_Integer node2 = 0;
            Standard_Integer node3 = 0;
            triangulation->Triangle(triangleIndex).Get(node1, node2, node3);
            if (reverse) {
                std::swap(node2, node3);
            }

            const gp_Pnt point1 = triangulation->Node(node1).Transformed(transform);
            const gp_Pnt point2 = triangulation->Node(node2).Transformed(transform);
            const gp_Pnt point3 = triangulation->Node(node3).Transformed(transform);

            gp_Dir normal1;
            gp_Dir normal2;
            gp_Dir normal3;
            if (triangulation->HasNormals()) {
                normal1 = triangulation->Normal(node1);
                normal2 = triangulation->Normal(node2);
                normal3 = triangulation->Normal(node3);
                if (reverse) {
                    normal1.Reverse();
                    normal2.Reverse();
                    normal3.Reverse();
                }
                normal1.Transform(transform);
                normal2.Transform(transform);
                normal3.Transform(transform);
            } else {
                gp_Vec normal(point1, point2);
                normal.Cross(gp_Vec(point1, point3));
                if (normal.SquareMagnitude() <= 1e-24) {
                    continue;
                }
                normal.Normalize();
                normal1 = gp_Dir(normal);
                normal2 = normal1;
                normal3 = normal1;
            }

            mesh.vertices.push_back({asFloat(point1), asFloat(normal1)});
            mesh.vertices.push_back({asFloat(point2), asFloat(normal2)});
            mesh.vertices.push_back({asFloat(point3), asFloat(normal3)});
        }
    }
    return mesh;
}

}  // namespace datumloop
