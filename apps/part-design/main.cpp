#include "datumloop/engineering/MotorCarrierDesign.hpp"
#include "datumloop/engineering/Manufacturing.hpp"
#include "datumloop/geometry/OcctKernel.hpp"
#include "datumloop/io/DldProjectStore.hpp"
#include "datumloop/io/PartRequirementsCodec.hpp"
#include <BRepTools.hxx>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <algorithm>

namespace {
void write(const QString& path,const QJsonObject& object) {
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly)) throw std::runtime_error("could not open output JSON");
    const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Indented);
    if(file.write(bytes)!=bytes.size()) throw std::runtime_error("could not finish output JSON");
}
}
int main(int argc,char* argv[]) {
    QCoreApplication app(argc,argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Generate an editable DatumLoop Z-spindle motor carrier from reviewed requirements");
    parser.addHelpOption();
    parser.addOption({"requirements","Reviewed part requirements JSON","file"});
    parser.addOption({"output","Candidate output directory","directory"});
    parser.addOption({"thickness","Explicit bounded thickness change in mm","mm"});
    parser.process(app);
    try {
        if(!parser.isSet("requirements") || !parser.isSet("output")) throw std::invalid_argument("--requirements and --output are required");
        auto part=datumloop::loadPartRequirements(parser.value("requirements"));
        if(parser.isSet("thickness")) {
            bool ok=false; const double value=parser.value("thickness").toDouble(&ok);
            if(!ok) throw std::invalid_argument("invalid thickness number");
            part.changeParameter("plate.thickness",value,"Explicit candidate thickness study; preserve every motor and gantry mating dimension");
        }
        auto candidate=datumloop::generateMotorCarrier(std::move(part));
        const QDir output(parser.value("output"));
        if(!QDir().mkpath(output.absolutePath())) throw std::runtime_error("could not create candidate directory");
        // Retain each candidate in its own directory; source generation never runs FEA.
        if(QFile::exists(output.filePath("carrier.dld"))) throw std::invalid_argument("candidate already exists; choose a new output directory");
        datumloop::DldProjectStore::save(candidate.document,output.filePath("carrier.dld"));
        const auto* object=candidate.document.object(candidate.cadObjectId);
        if(!BRepTools::Write(object->shape,output.filePath("carrier.brep").toStdString().c_str())) throw std::runtime_error("could not write exact OCCT BRep");
        QJsonArray holes;
        for(const auto& hole:candidate.holes) holes.append(QJsonObject{{"name",QString::fromStdString(hole.name)},
            {"role",QString::fromStdString(hole.semanticRole)},{"x_mm",hole.xMm},{"y_mm",hole.yMm},{"diameter_mm",hole.diameterMm}});
        const auto& stored=candidate.document.engineeringModel().partRequirements.front();
        const auto& material=candidate.document.electronicsModel().materials.front();
        const auto density=std::find_if(material.propertyModels.begin(),material.propertyModels.end(),
            [](const auto& property){return property.propertyName=="mass.density";});
        if(density==material.propertyModels.end() || !density->constantValue || density->propertyUnit!="kg/m^3")
            throw std::invalid_argument("candidate has no sourced reference density");
        const auto manufacturing=datumloop::evaluateMotorCarrierManufacturing(candidate);
        QJsonArray checks,limitations;
        for(const auto& check:manufacturing.checks) {
            QJsonArray requirements;
            for(const auto& id:check.constraint.requirementIds) requirements.append(QString::fromStdString(id));
            checks.append(QJsonObject{{"id",QString::fromStdString(check.constraint.id)},
                {"status",QString::fromStdString(datumloop::manufacturingCheckStatusName(check.status))},
                {"source_requirement_ids",requirements},{"method",QString::fromStdString(check.constraint.method)},
                {"observed",check.observed?QJsonValue(*check.observed):QJsonValue(QJsonValue::Null)},
                {"limit",check.limit?QJsonValue(*check.limit):QJsonValue(QJsonValue::Null)},
                {"unit",QString::fromStdString(check.unit)},{"reason",QString::fromStdString(check.reason)}});
        }
        for(const auto& limitation:manufacturing.limitations) limitations.append(QString::fromStdString(limitation));
        write(output.filePath("manufacturing-report.json"),{{"cad_object_id",QString::fromStdString(manufacturing.cadObjectId)},
            {"document_revision",static_cast<double>(manufacturing.documentRevision)},
            {"geometric_screening_passed",manufacturing.geometricScreeningPassed()},
            {"process_qualified",false},{"checks",checks},{"limitations",limitations}});
        write(output.filePath("requirements.json"),datumloop::encodePartRequirements(stored));
        write(output.filePath("geometry-report.json"),{{"project_id",QString::fromStdString(candidate.document.projectId())},
            {"cad_object_id",QString::fromStdString(candidate.cadObjectId)},
            {"document_revision",static_cast<double>(candidate.document.revision())},
            {"exact_occt_valid",object->metrics.valid},{"feature_count",static_cast<int>(object->featureGraph.features().size())},
            {"width_mm",object->metrics.maximum[0]-object->metrics.minimum[0]},
            {"height_mm",object->metrics.maximum[1]-object->metrics.minimum[1]},
            {"thickness_mm",object->metrics.maximum[2]-object->metrics.minimum[2]},
            {"volume_mm3",object->metrics.volume},{"holes",holes},
            {"material","6061-T6; NIST compiled reference properties"},
            {"nominal_mass_kg",object->metrics.volume*1e-9*(*density->constantValue)},
            {"fea_status","not_run"},{"manufacturing_geometric_screening_passed",manufacturing.geometricScreeningPassed()},
            {"geometry_classification","reference application candidate; not released for manufacture"}});
        const auto mesh=datumloop::OcctKernel::tessellate(object->shape,0.04,0.15);
        QJsonArray vertices;
        for(const auto& vertex:mesh.vertices) vertices.append(QJsonArray{vertex.position[0],vertex.position[1],vertex.position[2]});
        write(output.filePath("display-mesh.json"),{{"unit","mm"},{"triangle_vertices",vertices}});
        std::cout<<"Exact editable carrier generated: "<<holes.size()<<" holes, "<<object->metrics.volume<<" mm^3; FEA not run.\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
    return 0;
}
