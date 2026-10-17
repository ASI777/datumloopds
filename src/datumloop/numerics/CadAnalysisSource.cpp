#include "datumloop/numerics/AnalysisMesh.hpp"
#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/topology/DatumTopologyResolver.hpp"
#include "datumloop/geometry/OcctKernel.hpp"
#include <BRepTools.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Pln.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Vec.hxx>
#include <TopTools_FormatVersion.hxx>
#include <QCryptographicHash>
#include <QByteArray>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <set>
namespace datumloop {
std::string analysisBrepBytes(const DatumObject& object) {
    if(object.shape.IsNull()) throw std::invalid_argument("analysis source has no exact geometry");
    // BRep display meshing can populate edge polygons and cache/check flags even
    // when triangulations are excluded from Write. Export a clean independent
    // geometry copy and normalize non-geometric flags; never clean the CAD body.
    TopoDS_Shape nominal=BRepBuilderAPI_Copy(object.shape,Standard_True,Standard_False).Shape();
    BRepTools::Clean(nominal,Standard_True);
    TopTools_IndexedMapOfShape topology;TopExp::MapShapes(nominal,topology);
    for(int i=1;i<=topology.Extent();++i) {
        auto shape=topology(i);shape.Free(Standard_False);shape.Modified(Standard_False);
        shape.Checked(Standard_False);shape.Convex(Standard_False);
    }
    std::ostringstream stream;
    BRepTools::Write(nominal,stream,Standard_False,Standard_False,TopTools_FormatVersion_VERSION_3);
    if(!stream) throw std::runtime_error("could not serialize exact analysis geometry");
    return stream.str();
}
std::string analysisGeometrySha256(const DatumObject& object) {
    const auto bytes=analysisBrepBytes(object);
    return QCryptographicHash::hash(QByteArray(bytes.data(),static_cast<qsizetype>(bytes.size())),QCryptographicHash::Sha256).toHex().toStdString();
}
void AnalysisMeshingRequest::validateAgainst(const DatumDocument& document) const {
    if(!isDatumUuid(sourceProjectId) || sourceProjectId!=document.projectId() ||
        sourceDocumentRevision!=document.revision() || !isDatumUuid(cadObjectId) || !document.object(cadObjectId))
        throw std::invalid_argument("meshing request source identity is invalid or stale");
    if(!std::isfinite(settings.maximumElementSizeM) || settings.maximumElementSizeM<=0 ||
        !std::isfinite(settings.maximumElementAspectRatio) || settings.maximumElementAspectRatio<1 ||
        settings.maximumElementCount==0 || settings.maximumElementCount>2000000 || !std::isfinite(settings.minimumScaledJacobian) ||
        settings.minimumScaledJacobian<=0 || settings.minimumScaledJacobian>1 ||
        !std::isfinite(settings.maximumBoundaryAreaRelativeError) || settings.maximumBoundaryAreaRelativeError<=0 ||
        settings.maximumBoundaryAreaRelativeError>0.1 || !std::isfinite(settings.maximumVolumeRelativeError) ||
        settings.maximumVolumeRelativeError<=0 || settings.maximumVolumeRelativeError>0.1)
        throw std::invalid_argument("meshing request quality, size or resource limits are invalid");
}
void validateTetrahedralMeshAgainstCad(const DatumDocument& document,const AnalysisMesh& mesh,
                                       double maximumAreaError,double maximumVolumeError) {
    mesh.validate();
    const auto* object=document.object(mesh.cadObjectId);
    if(!object||mesh.sourceProjectId!=document.projectId()||mesh.sourceDocumentRevision!=document.revision()||
        mesh.geometrySha256!=analysisGeometrySha256(*object))
        throw std::invalid_argument("tetrahedral mesh source geometry/revision is stale");
    if(mesh.tetrahedra.empty()) throw std::invalid_argument("expected a tetrahedral mesh");
    constexpr double toleranceMm=1e-4;
    for(const auto& region:mesh.boundaryRegions) {
        const auto reference=std::find_if(document.topologyReferences().begin(),document.topologyReferences().end(),
            [&](const auto& r){return r.id==region.topologyReferenceId&&r.objectId==object->id&&r.subshapeType==DatumSubshapeType::Face;});
        if(reference==document.topologyReferences().end()) throw std::invalid_argument("mesh region references unknown CAD face");
        const auto resolved=DatumTopologyResolver::resolve(*reference,object->id,object->shape,{},object->featureGraph.outputFeatureId());
        if(resolved.status!=DatumTopologyResolutionStatus::Resolved) throw std::invalid_argument("mesh CAD boundary is ambiguous");
        const auto face=TopoDS::Face(resolved.subshape);BRepAdaptor_Surface surface(face);
        GProp_GProps props;BRepGProp::SurfaceProperties(face,props);const double cadArea=props.Mass()*1e-6;
        if(std::abs(region.exactCadAreaM2-cadArea)>cadArea*1e-8||std::abs(region.areaM2-cadArea)/cadArea>maximumAreaError)
            throw std::invalid_argument("attributed mesh area disagrees with its exact CAD face");
        std::set<std::uint32_t> nodes;
        for(const auto& triangle:mesh.boundaryTriangles) if(triangle.attribute==region.attribute)
            nodes.insert(triangle.nodeIndices.begin(),triangle.nodeIndices.end());
        if(nodes.empty()) throw std::invalid_argument("CAD boundary has no mesh samples");
        for(auto index:nodes) {
            const auto& x=mesh.nodes[index].positionM;const gp_Pnt point(x[0]*1000,x[1]*1000,x[2]*1000);
            double distance=0;
            if(surface.GetType()==GeomAbs_Plane) distance=surface.Plane().Distance(point);
            else if(surface.GetType()==GeomAbs_Cylinder) {
                const auto cylinder=surface.Cylinder();const auto axis=cylinder.Axis();
                const gp_Vec delta(axis.Location(),point);const auto direction=axis.Direction();
                const double projection=delta.X()*direction.X()+delta.Y()*direction.Y()+delta.Z()*direction.Z();
                distance=std::abs(std::sqrt(std::max(0.,delta.SquareMagnitude()-projection*projection))-cylinder.Radius());
            } else throw std::invalid_argument("unqualified imported CAD surface type");
            BRepClass_FaceClassifier classifier(face,point,toleranceMm);
            if(distance>toleranceMm||(classifier.State()!=TopAbs_IN&&classifier.State()!=TopAbs_ON))
                throw std::invalid_argument("mesh boundary attribute points do not belong to its persistent CAD face");
        }
    }
    BRepClass3d_SolidClassifier classifier(object->shape);
    for(const auto& n:mesh.nodes) {
        const auto& x=n.positionM;classifier.Perform(gp_Pnt(x[0]*1000,x[1]*1000,x[2]*1000),toleranceMm);
        if(classifier.State()!=TopAbs_IN&&classifier.State()!=TopAbs_ON) throw std::invalid_argument("mesh node is outside the source solid");
    }
    double volume=0;
    for(const auto& t:mesh.tetrahedra) {
        const auto& p=mesh.nodes[t.nodeIndices[0]].positionM;
        const auto& a=mesh.nodes[t.nodeIndices[1]].positionM;
        const auto& b=mesh.nodes[t.nodeIndices[2]].positionM;
        const auto& c=mesh.nodes[t.nodeIndices[3]].positionM;
        const gp_Vec u(a[0]-p[0],a[1]-p[1],a[2]-p[2]);
        const gp_Vec v(b[0]-p[0],b[1]-p[1],b[2]-p[2]);
        const gp_Vec w(c[0]-p[0],c[1]-p[1],c[2]-p[2]);
        volume+=u.Dot(v.Crossed(w))/6.;
    }
    const double cadVolume=OcctKernel::measure(object->shape).volume*1e-9;
    if(std::abs(volume-cadVolume)/cadVolume>maximumVolumeError)
        throw std::invalid_argument("tetrahedral volume does not approximate the exact CAD domain");
}
} // namespace datumloop
