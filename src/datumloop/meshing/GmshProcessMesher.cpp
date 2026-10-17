#include "datumloop/meshing/GmshProcessMesher.hpp"
#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/geometry/OcctKernel.hpp"
#include "datumloop/topology/DatumTopologyResolver.hpp"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace datumloop {
namespace {
void need(bool b,const char* why) {if(!b) throw std::invalid_argument(why);}
void write(const QString& path,const QByteArray& bytes) {
    QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()) throw std::runtime_error("could not write meshing input");
}
QJsonArray array(const QJsonObject& o,const char* key) {auto v=o.value(QLatin1String(key));need(v.isArray(),"invalid worker result array");return v.toArray();}
double number(const QJsonValue& v) {need(v.isDouble()&&std::isfinite(v.toDouble()),"missing or non-finite worker number");return v.toDouble();}
std::uint32_t index(const QJsonValue& v) {const auto n=number(v);need(n>=0&&std::floor(n)==n&&n<=std::numeric_limits<std::uint32_t>::max(),"worker index out of bounds");return static_cast<std::uint32_t>(n);}
Vector3 vector(const QJsonValue& v) {need(v.isArray()&&v.toArray().size()==3,"worker vector must have three coordinates");const auto a=v.toArray();return {number(a[0]),number(a[1]),number(a[2])};}
QJsonArray json(const Vector3& v) {return {v[0],v[1],v[2]};}
}
GmshProcessMesher::GmshProcessMesher(GmshWorkerSettings settings):worker_(std::move(settings)) {
    need(!worker_.pythonExecutable.empty()&&!worker_.workerScript.empty()&&worker_.timeoutMilliseconds>0&&
        worker_.timeoutMilliseconds<=600000&&worker_.maximumMemoryMiB>=256&&worker_.maximumMemoryMiB<=8192,
        "invalid Gmsh worker configuration");
}
AnalysisMesh GmshProcessMesher::generate(const DatumDocument& document,const AnalysisMeshingRequest& request) const {
    request.validateAgainst(document);
    const auto* object=document.object(request.cadObjectId);
    need(object&&!object->shape.IsNull()&&OcctKernel::isValid(object->shape),"mesher needs exact valid OCCT geometry");
    int solidCount=0,faceCount=0;
    for(TopExp_Explorer it(object->shape,TopAbs_SOLID);it.More();it.Next()) ++solidCount;
    for(TopExp_Explorer it(object->shape,TopAbs_FACE);it.More();it.Next()) ++faceCount;
    need(solidCount==1,"initial external mesher requires a single solid");
    std::vector<const DatumTopologyReference*> faces;
    for(const auto& reference:document.topologyReferences()) if(reference.objectId==object->id&&reference.subshapeType==DatumSubshapeType::Face) faces.push_back(&reference);
    need(static_cast<int>(faces.size())==faceCount,"persistent CAD faces do not cover the source solid");
    std::sort(faces.begin(),faces.end(),[](auto a,auto b){return a->id<b->id;});
    QJsonArray faceData;int attribute=1;
    for(const auto* reference:faces) {
        auto resolved=DatumTopologyResolver::resolve(*reference,object->id,object->shape,{},object->featureGraph.outputFeatureId());
        need(resolved.status==DatumTopologyResolutionStatus::Resolved&&resolved.subshape.ShapeType()==TopAbs_FACE,"CAD face UUID did not resolve uniquely before meshing");
        const auto face=TopoDS::Face(resolved.subshape);BRepAdaptor_Surface surface(face);
        std::string type;Vector3 normal{0,0,0};
        if(surface.GetType()==GeomAbs_Plane) {
            type="plane";auto direction=surface.Plane().Axis().Direction();
            const double sign=face.Orientation()==TopAbs_REVERSED?-1.:1.;
            normal={sign*direction.X(),sign*direction.Y(),sign*direction.Z()};
        } else if(surface.GetType()==GeomAbs_Cylinder) type="cylinder";
        else throw std::invalid_argument("external face fingerprint is currently qualified for plane and cylinder faces only");
        GProp_GProps props;BRepGProp::SurfaceProperties(face,props);
        const auto center=props.CentreOfMass();Bnd_Box bounds;BRepBndLib::AddOptimal(face,bounds,Standard_False,Standard_False);
        double xmin,ymin,zmin,xmax,ymax,zmax;bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
        faceData.append(QJsonObject{{"topology_reference_id",QString::fromStdString(reference->id)},
            {"attribute",attribute++},{"surface_type",QString::fromStdString(type)},
            {"area_mm2",props.Mass()},{"centroid_mm",QJsonArray{center.X(),center.Y(),center.Z()}},
            {"bounds_mm",QJsonArray{xmin,ymin,zmin,xmax,ymax,zmax}},{"outward_normal",json(normal)}});
    }
    QTemporaryDir directory;
    if(!directory.isValid()) throw std::runtime_error("could not create isolated meshing directory");
    const QString brepPath=directory.filePath("source.brep"),requestPath=directory.filePath("request.json"),resultPath=directory.filePath("result.json");
    const auto bytes=analysisBrepBytes(*object);write(brepPath,QByteArray::fromStdString(bytes));
    const auto digest=analysisGeometrySha256(*object);
    const double cadVolume=object->metrics.volume*1e-9;
    need(std::isfinite(cadVolume)&&cadVolume>0,"invalid CAD domain volume");
    const auto& s=request.settings;
    const QJsonObject input{{"schema_version",1},{"brep_path",brepPath},{"geometry_sha256",QString::fromStdString(digest)},
        {"source_project_id",QString::fromStdString(document.projectId())},
        {"source_document_revision",QString::number(document.revision())},{"cad_object_id",QString::fromStdString(object->id)},
        {"exact_cad_volume_m3",cadVolume},{"faces",faceData},{"maximum_memory_mib",static_cast<int>(worker_.maximumMemoryMiB)},
        {"settings",QJsonObject{{"maximum_element_size_m",s.maximumElementSizeM},
            {"maximum_element_count",static_cast<double>(s.maximumElementCount)},
            {"maximum_element_aspect_ratio",s.maximumElementAspectRatio},{"minimum_scaled_jacobian",s.minimumScaledJacobian},
            {"maximum_boundary_area_relative_error",s.maximumBoundaryAreaRelativeError},{"maximum_volume_relative_error",s.maximumVolumeRelativeError}}}};
    write(requestPath,QJsonDocument(input).toJson(QJsonDocument::Compact));
    QProcess process;
    process.setProgram(QString::fromStdString(worker_.pythonExecutable));
    process.setArguments({QString::fromStdString(worker_.workerScript),requestPath,resultPath});
    process.start();
    if(!process.waitForStarted(10000)) throw std::runtime_error("could not start configured Gmsh worker");
    if(!process.waitForFinished(static_cast<int>(worker_.timeoutMilliseconds))) {
        process.kill();process.waitForFinished(5000);throw std::runtime_error("Gmsh worker exceeded its timeout");
    }
    const auto error=process.readAllStandardError();
    if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0)
        throw std::runtime_error("Gmsh worker rejected the request: "+error.toStdString());
    const QFileInfo resultInfo(resultPath);
    need(resultInfo.size()>0&&resultInfo.size()<=256*1024*1024,"Gmsh result exceeds the import size cap");
    QFile resultFile(resultPath);if(!resultFile.open(QIODevice::ReadOnly)) throw std::runtime_error("worker returned no mesh result");
    QJsonParseError parseError;const auto parsed=QJsonDocument::fromJson(resultFile.readAll(),&parseError);
    need(parseError.error==QJsonParseError::NoError&&parsed.isObject(),"worker returned malformed JSON");
    const auto result=parsed.object();
    need(number(result.value("schema_version"))==1&&result.value("backend").toString()=="gmsh.occ.external"&&
        result.value("version").toString()=="4.15.2"&&result.value("element_type").toString()=="tetrahedron4"&&
        result.value("unit").toString()=="m"&&result.value("geometry_sha256").toString().toStdString()==digest&&
        result.value("source_project_id")==input.value("source_project_id")&&
        result.value("source_document_revision")==input.value("source_document_revision")&&
        result.value("cad_object_id")==input.value("cad_object_id"),"worker result source identity, units or backend mismatch");
    AnalysisMesh mesh;mesh.cadObjectId=object->id;mesh.sourceProjectId=document.projectId();
    mesh.backendBuildInfo=result.value("build_info").toString().toStdString();
    mesh.meshingWallTimeS=number(result.value("wall_time_s"));
    need(mesh.backendBuildInfo.find("OpenCASCADE")!=std::string::npos&&mesh.meshingWallTimeS>=0,"missing worker build or timing provenance");
    for(const auto& entry:array(result,"log")) {need(entry.isString(),"invalid worker log entry");mesh.mesherLog.push_back(entry.toString().toStdString());}
    mesh.sourceDocumentRevision=document.revision();mesh.geometrySha256=digest;
    mesh.provenance.revisionId=createDatumUuidV4();mesh.provenance.generator="gmsh.occ.external";
    mesh.provenance.generatorVersion="4.15.2";mesh.provenance.elementType="tetrahedron4";
    mesh.provenance.requestedMaximumElementSizeM=s.maximumElementSizeM;
    for(const auto& item:array(result,"nodes")) mesh.nodes.push_back({vector(item)});
    for(const auto& item:array(result,"tetrahedra")) {
        need(item.isArray()&&item.toArray().size()==4,"invalid worker tetrahedron");const auto a=item.toArray();
        mesh.tetrahedra.push_back({{index(a[0]),index(a[1]),index(a[2]),index(a[3])}});
    }
    for(const auto& item:array(result,"boundary_triangles")) {
        need(item.isArray()&&item.toArray().size()==4,"invalid worker boundary triangle");const auto a=item.toArray();
        const auto attr=index(a[3]);need(attr<=static_cast<unsigned>(std::numeric_limits<int>::max()),"boundary attribute overflow");
        mesh.boundaryTriangles.push_back({{index(a[0]),index(a[1]),index(a[2])},static_cast<int>(attr)});
    }
    for(const auto& item:array(result,"boundary_regions")) {
        need(item.isObject(),"invalid worker boundary region");const auto r=item.toObject();
        const auto attr=index(r.value("attribute"));need(attr<=static_cast<unsigned>(std::numeric_limits<int>::max()),"region attribute overflow");
        mesh.boundaryRegions.push_back({r.value("topology_reference_id").toString().toStdString(),static_cast<int>(attr),
            vector(r.value("outward_normal")),number(r.value("area_m2")),number(r.value("exact_cad_area_m2"))});
    }
    for(const auto* face:faces) mesh.topologyReferenceIds.push_back(face->id);
    mesh.provenance.nodeCount=mesh.nodes.size();mesh.provenance.elementCount=mesh.tetrahedra.size();
    mesh.provenance.minimumScaledJacobian=number(result.value("minimum_scaled_jacobian"));
    mesh.provenance.maximumAspectRatio=number(result.value("maximum_aspect_ratio"));
    need(mesh.provenance.elementCount<=s.maximumElementCount&&mesh.provenance.minimumScaledJacobian>=s.minimumScaledJacobian&&
        mesh.provenance.maximumAspectRatio<=s.maximumElementAspectRatio,"worker mesh exceeds requested element or quality limits");
    need(number(result.value("relative_volume_error"))<=s.maximumVolumeRelativeError&&
        number(result.value("maximum_boundary_area_relative_error"))<=s.maximumBoundaryAreaRelativeError,"worker geometric approximation exceeds request");
    mesh.validate();
    validateTetrahedralMeshAgainstCad(document,mesh,s.maximumBoundaryAreaRelativeError,s.maximumVolumeRelativeError);
    return mesh;
}
} // namespace datumloop
