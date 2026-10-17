#include "datumloop/numerics/AnalysisMesh.hpp"
#include "datumloop/core/DatumUuid.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
namespace datumloop {
namespace {
Vector3 sub(const Vector3& a,const Vector3& b) { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
Vector3 cross(const Vector3& a,const Vector3& b) {return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
double dot(const Vector3& a,const Vector3& b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
double length(const Vector3& p) {return std::hypot(p[0],p[1],p[2]);}
void need(bool b,const char* why) {if(!b) throw std::invalid_argument(why);}
bool close(double a,double b) {return std::abs(a-b)<=std::max({std::abs(a),std::abs(b),1e-30})*1e-8;}
}
void validateTetrahedralAnalysisMesh(const AnalysisMesh& m) {
    need(isDatumUuid(m.provenance.revisionId) && isDatumUuid(m.cadObjectId) && isDatumUuid(m.sourceProjectId),"invalid tetrahedral mesh identity");
    need(m.geometrySha256.size()==64 && m.geometrySha256.find_first_not_of("0123456789abcdef")==std::string::npos,"tetrahedral mesh lacks its exact CAD digest");
    need(m.provenance.generator=="gmsh.occ.external" && m.provenance.generatorVersion=="4.15.2" &&
        m.provenance.elementType=="tetrahedron4" && m.elements.empty() && m.boundaryElements.empty(),"unsupported tetrahedral backend or mixed mesh");
    need(m.nodes.size()>=4 && !m.tetrahedra.empty() && !m.boundaryTriangles.empty() &&
        m.provenance.nodeCount==m.nodes.size() && m.provenance.elementCount==m.tetrahedra.size(),"invalid tetrahedral mesh counts");
    need(std::isfinite(m.provenance.requestedMaximumElementSizeM) && m.provenance.requestedMaximumElementSizeM>0,"invalid tetrahedral mesh size setting");
    std::set<std::string> references,regionReferences;std::set<int> attributes;
    for(const auto& id:m.topologyReferenceIds) need(isDatumUuid(id)&&references.insert(id).second,"invalid or duplicate CAD face UUID");
    std::map<int,double> regionAreas;
    for(const auto& r:m.boundaryRegions) {
        need(isDatumUuid(r.topologyReferenceId)&&regionReferences.insert(r.topologyReferenceId).second&&
            r.attribute>0&&attributes.insert(r.attribute).second,"ambiguous tetrahedral boundary map");
        need(std::isfinite(r.areaM2)&&r.areaM2>0&&std::isfinite(r.exactCadAreaM2)&&r.exactCadAreaM2>0,
            "invalid mesh/CAD boundary area");
        for(double v:r.outwardNormal) need(std::isfinite(v),"invalid boundary normal");
        regionAreas[r.attribute]=r.areaM2;
    }
    need(references.size()>=4 && references==regionReferences,"boundary groups do not cover all CAD faces");
    for(const auto& n:m.nodes) for(double x:n.positionM) need(std::isfinite(x),"non-finite mesh coordinate");
    struct Face {int count=0;std::uint32_t opposite=0;};
    std::map<std::array<std::uint32_t,3>,Face> faces;
    std::vector<bool> used(m.nodes.size(),false);
    double minimumJacobian=1,maximumAspect=1;
    for(const auto& t:m.tetrahedra) {
        std::set<std::uint32_t> unique;
        for(auto n:t.nodeIndices) {need(n<m.nodes.size()&&unique.insert(n).second,"invalid tetrahedron connectivity");used[n]=true;}
        std::array<Vector3,4> p;
        for(int i=0;i<4;++i) p[i]=m.nodes[t.nodeIndices[i]].positionM;
        const double determinant=dot(sub(p[1],p[0]),cross(sub(p[2],p[0]),sub(p[3],p[0])));
        need(std::isfinite(determinant)&&determinant>0,"inverted or degenerate tetrahedron");
        double minEdge=std::numeric_limits<double>::infinity(),maxEdge=0;
        for(int i=0;i<4;++i) for(int j=i+1;j<4;++j) {const double e=length(sub(p[i],p[j]));minEdge=std::min(minEdge,e);maxEdge=std::max(maxEdge,e);}
        maximumAspect=std::max(maximumAspect,maxEdge/minEdge);
        for(int v=0;v<4;++v) {
            double denominator=1;
            std::array<std::uint32_t,3> face{};int k=0;
            for(int j=0;j<4;++j) if(j!=v) {denominator*=length(sub(p[j],p[v]));face[k++]=t.nodeIndices[j];}
            minimumJacobian=std::min(minimumJacobian,determinant/denominator);
            std::sort(face.begin(),face.end());auto& f=faces[face];++f.count;f.opposite=t.nodeIndices[v];
            need(f.count<=2,"nonmanifold tetrahedral volume mesh");
        }
    }
    need(std::all_of(used.begin(),used.end(),[](bool x){return x;}),"tetrahedral mesh contains orphan nodes");
    need(close(minimumJacobian,m.provenance.minimumScaledJacobian)&&close(maximumAspect,m.provenance.maximumAspectRatio),"tetrahedral quality metadata disagree with actual geometry");
    std::set<std::array<std::uint32_t,3>> boundaryFaces;std::map<int,double> measuredAreas;
    for(const auto& b:m.boundaryTriangles) {
        need(attributes.contains(b.attribute),"triangle references an unknown CAD boundary attribute");
        auto key=b.nodeIndices;std::sort(key.begin(),key.end());
        const auto f=faces.find(key);
        need(f!=faces.end()&&f->second.count==1&&boundaryFaces.insert(key).second,"duplicate, interior or unsupported boundary triangle");
        const auto& a=m.nodes[b.nodeIndices[0]].positionM;const auto& c=m.nodes[b.nodeIndices[1]].positionM;const auto& d=m.nodes[b.nodeIndices[2]].positionM;
        const auto normal=cross(sub(c,a),sub(d,a));
        need(dot(normal,sub(m.nodes[f->second.opposite].positionM,a))<0,"boundary triangle does not point out of the volume");
        measuredAreas[b.attribute]+=length(normal)*0.5;
    }
    std::size_t exterior=0;for(const auto& [key,f]:faces) { (void)key;if(f.count==1)++exterior; }
    need(exterior==boundaryFaces.size(),"tetrahedral exterior has unattributed boundary faces");
    for(const auto& [attribute,area]:regionAreas) need(close(area,measuredAreas[attribute]),"boundary area disagrees with attributed triangles");
}
} // namespace datumloop
