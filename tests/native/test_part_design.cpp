#include "datumloop/core/DatumDocument.hpp"
#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/io/DldProjectStore.hpp"
#include "datumloop/io/PartRequirementsCodec.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* reason) { if(!condition) throw std::runtime_error(reason); }
template<class F> void rejects(F action, const char* reason) {
    bool rejected=false; try { action(); } catch(const std::exception&) { rejected=true; }
    require(rejected,reason);
}
}
int main(int argc,char* argv[]) {
    QCoreApplication app(argc,argv);
    try {
        auto part=datumloop::loadPartRequirements(QString::fromUtf8(DATUMLOOP_PART_SOURCE));
        require(part.requirements.size()==18 && part.interfaces.size()==2 && part.sources.size()==2,
                "manufacturer requirements inventory changed without review");
        require(std::abs(part.parameterIn("motor.mount.pcd","m")-0.016)<1e-15,
                "source millimetres were not converted to SI");
        require(part.unresolvedCriticalRequirements().size()==7,
                "critical unknown machine requirements were silently resolved");
        const auto source=part;
        rejects([&]{part.changeParameter("motor.mount.pcd",17,"improve stiffness");},
                "automatic redesign changed a fixed motor mating dimension");
        require(part==source,"rejected fixed-interface change mutated requirements");
        auto malformed=datumloop::encodePartRequirements(part);
        auto requirements=malformed.value("requirements").toArray();
        auto unknown=requirements.last().toObject(); unknown.insert("value",0); requirements.replace(requirements.size()-1,unknown);
        malformed.insert("requirements",requirements);
        rejects([&]{(void)datumloop::decodePartRequirements(malformed);},"UNKNOWN requirement accepted a fabricated zero");
        malformed=datumloop::encodePartRequirements(part); malformed.insert("schemaVersion",2);
        rejects([&]{(void)datumloop::decodePartRequirements(malformed);},"unknown ingestion schema accepted");
        auto invalid=part; invalid.sources.front().sha256="invalid";
        rejects([&]{invalid.validate();},"invalid source checksum accepted");
        invalid=part; invalid.requirements.push_back(invalid.requirements.front());
        rejects([&]{invalid.validate();},"duplicate requirement key accepted");
        rejects([&]{(void)datumloop::convertRequirementValue(1,"mm","N*m");},"dimensionally incompatible requirement conversion accepted");

        datumloop::Requirement thickness;
        thickness.id="design.thickness"; thickness.description="Explicit test design thickness";
        thickness.status=datumloop::RequirementStatus::Required; thickness.value=1.5; thickness.unit="mm";
        thickness.evidence={part.sources.back().id,std::nullopt,"Controlled design selection",
            datumloop::RequirementEvidenceKind::DesignSelection,"Synthetic test choice with explicit bounds",true,{},std::nullopt};
        part.requirements.push_back(thickness);
        part.variables.push_back({"plate.thickness",thickness.id,datumloop::ParameterRole::BoundedVariable,
            1.5,1.5,5.5,thickness.evidence});
        const auto before=part;
        rejects([&]{part.changeParameter("plate.thickness",6,"increase stiffness");},"out-of-bound redesign accepted");
        require(part==before,"rejected out-of-bound change modified the candidate");
        part.changeParameter("plate.thickness",2.5,"Increase bending stiffness while preserving the motor interface");
        require(part.variable("plate.thickness").value==2.5 && part.changes.size()==1 &&
                    part.changes.front().previousValue==1.5 && part.variable("motor.mount.pcd").value==16,
                "bounded redesign did not preserve provenance and fixed dimensions");
        auto evaluation=part.evaluate("candidate-1","geometry-1",
            {{"relative_interface_displacement",3.7,"um","solver-run-test"}});
        require(!evaluation.passed() && evaluation.checks.front().status==datumloop::DesignCheckStatus::Unknown,
                "unknown precision target was treated as a passed design");
        auto& displacement=*std::find_if(part.requirements.begin(),part.requirements.end(),
            [](const auto& r){return r.id=="machine.interface.displacement";});
        displacement.status=datumloop::RequirementStatus::Required; displacement.value=5;
        displacement.evidence=thickness.evidence;
        evaluation=part.evaluate("candidate-2","geometry-2",
            {{"relative_interface_displacement",15e-6,"m","solver-run-test"}});
        require(evaluation.checks.front().status==datumloop::DesignCheckStatus::Failed && !evaluation.passed(),
                "design evaluator failed to detect displacement above an explicit target");
        evaluation=part.evaluate("candidate-3","geometry-3",
            {{"relative_interface_displacement",3.7e-6,"m","solver-run-test"}});
        require(evaluation.checks.front().status==datumloop::DesignCheckStatus::Passed && !evaluation.passed(),
                "passing one criterion bypassed unresolved machine requirements");

        datumloop::DatumDocument document("Requirements persistence");
        datumloop::DatumEngineeringModel model; model.partRequirements={part}; document.setEngineeringModel(model);
        QTemporaryDir directory; require(directory.isValid(),"temporary project directory unavailable");
        const auto path=directory.filePath("part.dld"); datumloop::DldProjectStore::save(document,path);
        const auto loaded=datumloop::DldProjectStore::load(path);
        require(loaded.engineeringModel()==model,"DLD round trip lost requirements, source evidence, interfaces or changes");
        QFile file(path); require(file.open(QIODevice::ReadOnly),"could not read saved project");
        auto root=QJsonDocument::fromJson(file.readAll()).object(); file.close();
        require(root.value("schemaVersion").toInt()==datumloop::DldProjectStore::SchemaVersion,"requirements were written using the wrong project schema");
        root.insert("schemaVersion",8);
        require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"could not write old-schema fixture");
        file.write(QJsonDocument(root).toJson()); file.close();
        rejects([&]{(void)datumloop::DldProjectStore::load(path);},"old schema silently accepted unsupported part data");
        auto oldModel=root.value("engineeringModel").toObject(); oldModel.remove("partRequirements");
        root.insert("engineeringModel",oldModel);
        require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"could not write schema-eight migration fixture");
        file.write(QJsonDocument(root).toJson()); file.close();
        require(datumloop::DldProjectStore::load(path).engineeringModel().partRequirements.empty(),
                "schema-eight project did not migrate with empty part requirements");
        auto collision=source; collision.id=document.projectId(); model.partRequirements={collision};
        rejects([&]{document.setEngineeringModel(model);},"part UUID collided with the document UUID");
        std::cout<<"Part requirements: 11 source values, 7 explicit critical unknowns, 2 interfaces; fixed-interface protection, bounded edits, quantitative evaluation and DLD migration passed.\n";
    } catch(const std::exception& error) {std::cerr<<"Part design failed: "<<error.what()<<'\n';return 1;}
    return 0;
}
