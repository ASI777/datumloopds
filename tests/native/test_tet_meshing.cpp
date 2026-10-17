#include "datumloop/engineering/MotorCarrierDesign.hpp"
#include "datumloop/geometry/OcctKernel.hpp"
#include "datumloop/io/PartRequirementsCodec.hpp"
#include "datumloop/meshing/GmshProcessMesher.hpp"
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
void need(bool b,const char* why) {if(!b) throw std::runtime_error(why);}
template<class F>void rejects(F action,const char* why) {bool failed=false;try{action();}catch(const std::exception&){failed=true;}need(failed,why);}
}
int main(int argc,char* argv[]) {
    QCoreApplication app(argc,argv);
    if(std::string(DATUMLOOP_GMSH_PYTHON).empty()) {std::cout<<"SKIPPED: no isolated Gmsh 4.15.2 interpreter configured\n";return 77;}
    try {
        auto candidate=datumloop::generateMotorCarrier(datumloop::loadPartRequirements(QString::fromUtf8(DATUMLOOP_CARRIER_STUDY)));
        const auto* object=candidate.document.object(candidate.cadObjectId);
        datumloop::AnalysisMeshingRequest request{candidate.document.projectId(),candidate.document.revision(),candidate.cadObjectId,{}};
        request.settings.maximumElementSizeM=0.0012;
        request.settings.maximumElementAspectRatio=12;
        datumloop::GmshProcessMesher mesher({DATUMLOOP_GMSH_PYTHON,DATUMLOOP_GMSH_WORKER,120000,2048});
        const auto mesh=mesher.generate(candidate.document,request);
        need(mesh.elements.empty()&&mesh.tetrahedra.size()>100&&mesh.nodes.size()>100&&mesh.boundaryRegions.size()==14,
            "carrier did not produce a practical tetrahedral mesh with all CAD boundary groups");
        for(const auto& n:mesh.nodes) need(std::abs(n.positionM[0])<0.017001&&std::abs(n.positionM[1])<0.022001&&std::abs(n.positionM[2])<0.000751,
            "mesher did not convert millimetre CAD to SI coordinates");
        const auto before=datumloop::analysisGeometrySha256(*object);
        (void)datumloop::OcctKernel::tessellate(object->shape,0.05,0.2);
        need(before==datumloop::analysisGeometrySha256(*object),"display tessellation changed the nominal geometry digest");
        auto inverted=mesh;std::swap(inverted.tetrahedra.front().nodeIndices[1],inverted.tetrahedra.front().nodeIndices[2]);
        rejects([&]{inverted.validate();},"inverted tetrahedron accepted");
        auto missing=mesh;missing.boundaryTriangles.pop_back();
        rejects([&]{missing.validate();},"unattributed exterior face accepted");
        std::vector<std::size_t> boltRegions;
        for(std::size_t i=0;i<mesh.boundaryRegions.size();++i) {
            const auto& r=mesh.boundaryRegions[i];
            const auto ref=std::find_if(candidate.document.topologyReferences().begin(),candidate.document.topologyReferences().end(),[&](const auto& f){return f.id==r.topologyReferenceId;});
            if(ref!=candidate.document.topologyReferences().end()&&ref->geometrySignature.geometryType=="cylinder"&&std::abs(ref->geometrySignature.centroidMm[1]+15)<1e-6)
                boltRegions.push_back(i);
        }
        need(boltRegions.size()==2,"gantry cylindrical face groups are missing");
        auto swapped=mesh;
        std::swap(swapped.boundaryRegions[boltRegions[0]].topologyReferenceId,swapped.boundaryRegions[boltRegions[1]].topologyReferenceId);
        rejects([&]{datumloop::validateTetrahedralMeshAgainstCad(candidate.document,swapped,0.01,0.01);},
            "swapped equal-area CAD face UUID mapping was accepted");
        auto capped=request;capped.settings.maximumElementCount=1;
        rejects([&]{(void)mesher.generate(candidate.document,capped);},"mesher ignored its element/resource cap");
        if(argc==3&&std::string(argv[1])=="--report") {
            const QString path=QString::fromUtf8(argv[2]);QDir().mkpath(QFileInfo(path).absolutePath());
            QJsonArray groups;
            for(const auto& r:mesh.boundaryRegions) groups.append(QJsonObject{{"face_uuid",QString::fromStdString(r.topologyReferenceId)},
                {"attribute",r.attribute},{"mesh_area_m2",r.areaM2},{"cad_area_m2",r.exactCadAreaM2}});
            double meshVolume=0;
            for(const auto& tet:mesh.tetrahedra) {
                const auto& a=mesh.nodes[tet.nodeIndices[0]].positionM;
                double d[3][3];
                for(int i=0;i<3;++i) for(int j=0;j<3;++j)
                    d[i][j]=mesh.nodes[tet.nodeIndices[i+1]].positionM[j]-a[j];
                meshVolume+=(d[0][0]*(d[1][1]*d[2][2]-d[1][2]*d[2][1])
                            -d[0][1]*(d[1][0]*d[2][2]-d[1][2]*d[2][0])
                            +d[0][2]*(d[1][0]*d[2][1]-d[1][1]*d[2][0]))/6;
            }
            GProp_GProps properties;BRepGProp::VolumeProperties(object->shape,properties);
            const double cadVolume=properties.Mass()*1e-9;
            const QJsonObject report{{"backend",QString::fromStdString(mesh.provenance.generator)},
                {"version",QString::fromStdString(mesh.provenance.generatorVersion)},
                {"backend_build",QString::fromStdString(mesh.backendBuildInfo)},
                {"geometry_sha256",QString::fromStdString(mesh.geometrySha256)},
                {"source_project_id",QString::fromStdString(mesh.sourceProjectId)},
                {"source_document_revision",QString::number(mesh.sourceDocumentRevision)},
                {"requested_maximum_element_size_m",request.settings.maximumElementSizeM},
                {"maximum_element_count",static_cast<double>(request.settings.maximumElementCount)},
                {"minimum_scaled_jacobian_limit",request.settings.minimumScaledJacobian},
                {"maximum_edge_ratio_limit",request.settings.maximumElementAspectRatio},
                {"boundary_area_relative_error_limit",request.settings.maximumBoundaryAreaRelativeError},
                {"volume_relative_error_limit",request.settings.maximumVolumeRelativeError},
                {"mesh_volume_m3",meshVolume},{"exact_cad_volume_m3",cadVolume},
                {"volume_relative_error",std::abs(meshVolume-cadVolume)/cadVolume},
                {"nodes",static_cast<double>(mesh.nodes.size())},{"tetrahedra",static_cast<double>(mesh.tetrahedra.size())},
                {"boundary_triangles",static_cast<double>(mesh.boundaryTriangles.size())},{"boundary_groups",groups},
                {"minimum_vertex_scaled_jacobian",mesh.provenance.minimumScaledJacobian},
                {"maximum_edge_ratio",mesh.provenance.maximumAspectRatio},{"meshing_wall_time_s",mesh.meshingWallTimeS},
                {"nominal_geometry_preserved",true},{"inverted_elements_rejected",true},{"missing_boundary_rejected",true},
                {"swapped_cad_face_mapping_rejected",true},{"resource_cap_rejected",true},
                {"fea_status","not_run"}};
            QFile file(path);need(file.open(QIODevice::WriteOnly),"cannot write mesh report");
            const auto bytes=QJsonDocument(report).toJson(QJsonDocument::Indented);need(file.write(bytes)==bytes.size(),"mesh report write failed");
        }
        candidate.document.setProperty(candidate.cadObjectId,"test.revision-change",true);
        rejects([&]{request.validateAgainst(candidate.document);},"stale meshing request accepted");
        std::cout<<"Gmsh 4.15.2 carrier mesh passed: "<<mesh.nodes.size()<<" nodes, "<<mesh.tetrahedra.size()<<" tets, "<<mesh.boundaryRegions.size()<<" persistent face groups; FEA not run.\n";
    }catch(const std::exception& error){std::cerr<<"Tetrahedral meshing failed: "<<error.what()<<'\n';return 1;}
    return 0;
}
