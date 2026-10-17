#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/engineering/CarrierStructuralDesign.hpp"
#include "datumloop/io/PartRequirementsCodec.hpp"
#include "datumloop/engineering/Manufacturing.hpp"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace datumloop;
namespace {void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try{
    auto candidate=generateMotorCarrier(loadPartRequirements(QString::fromUtf8(DATUMLOOP_CARRIER_STUDY)));
    auto part=candidate.document.engineeringModel().partRequirements.front();
    const auto study=makeMotorCarrierStaticStudy(candidate);
    int fixed=0,forces=0,body=0;Vector3 resultant{};double torque=0;
    for(const auto& condition:study.boundaryConditions){
        if(const auto* f=std::get_if<FixedDisplacement>(&condition)){++fixed;need(f->constrainedComponents==std::array<bool,3>{true,true,true},"unexpected support components");}
        if(const auto* f=std::get_if<AppliedForce>(&condition)){
            const auto hole=candidate.holes.at(static_cast<std::size_t>(++forces));
            for(int j=0;j<3;++j)resultant[j]+=f->totalForceN[j];
            torque+=(hole.xMm*.001)*f->totalForceN[1]-(hole.yMm-8)*.001*f->totalForceN[0];
        }
        if(const auto* f=std::get_if<BodyForce>(&condition)){++body;need(std::abs(f->forceDensityNPerM3[2]-2710*(60+9.80665))<1e-9,"self inertia omitted or changed");}
    }
    need(fixed==2&&forces==3&&body==1,"load case changed its qualified idealization");
    need(std::abs(resultant[0])<1e-12&&std::abs(resultant[1])<1e-12&&std::abs(resultant[2]-2.792266)<1e-12,"motor mass load is not statically equivalent");
    need(std::abs(torque-.1935)<1e-12,"bolt forces do not reproduce selected reaction torque");
    // Synthetic rigid motion checks the interface metric extraction independently
    // of MFEM. This is a mathematical unit test, not a simulation result.
    AnalysisMesh mesh;mesh.cadObjectId=candidate.cadObjectId;mesh.provenance.revisionId=createDatumUuidV4();
    StructuralAnalysisRun run;run.converged=true;run.provenance.sourceProjectId=candidate.document.projectId();
    run.provenance.cadObjectId=candidate.cadObjectId;
    run.provenance.mesh.geometrySha256=analysisGeometrySha256(*candidate.document.object(candidate.cadObjectId));
    run.provenance.mesh.revisionId=mesh.provenance.revisionId;run.provenance.solverRunId=createDatumUuidV4();
    FieldDataset displacement;displacement.id=createDatumUuidV4();displacement.fieldType=FieldType::Displacement;
    displacement.location=FieldLocation::Nodes;displacement.components={"x","y","z"};displacement.units="m";
    displacement.meshRevisionId=mesh.provenance.revisionId;displacement.solverRunId=run.provenance.solverRunId;
    const Vector3 translation{2e-6,-3e-6,4e-6},rotation{1e-4,-2e-4,3e-4};
    const auto motor=part.interfaces.front();
    for(std::size_t i=0;i<4;++i){
        mesh.boundaryRegions.push_back({motor.topologyReferenceIds.at(i),static_cast<int>(i+1)});
        AnalysisBoundaryTriangle t;t.attribute=static_cast<int>(i+1);
        for(std::size_t j=0;j<3;++j){
            const double angle=2*std::acos(-1.)*static_cast<double>(j)/3;
            const auto& hole=candidate.holes[i];const Vector3 p{hole.xMm*.001+.0005*std::cos(angle),hole.yMm*.001+.0005*std::sin(angle),0};
            t.nodeIndices[j]=static_cast<std::uint32_t>(mesh.nodes.size());mesh.nodes.push_back({p});
            const Vector3 r{p[0],p[1]-.008,0};
            const Vector3 u{translation[0]-rotation[2]*r[1],translation[1]+rotation[2]*r[0],translation[2]+rotation[0]*r[1]-rotation[1]*r[0]};
            displacement.values.insert(displacement.values.end(),u.begin(),u.end());
        }mesh.boundaryTriangles.push_back(t);
    }
    for(const auto index:{0u,2u}) {
        const int attribute=static_cast<int>(5+index/2);
        mesh.boundaryRegions.push_back({part.interfaces[1].topologyReferenceIds[index],attribute});
        AnalysisBoundaryTriangle triangle;triangle.attribute=attribute;
        for(std::size_t j=0;j<3;++j){
            triangle.nodeIndices[j]=static_cast<std::uint32_t>(mesh.nodes.size());
            mesh.nodes.push_back({Vector3{.001*static_cast<double>(j==1),.001*static_cast<double>(j==2),0}});
            displacement.values.insert(displacement.values.end(),{0,0,0});
        }
        mesh.boundaryTriangles.push_back(triangle);
    }
    run.fields.push_back(displacement);
    const auto response=evaluateMotorCarrierResponse(candidate,mesh,run);
    for(std::size_t j=0;j<3;++j){need(std::abs(response.meanMotorDisplacementM[j]-translation[j])<1e-15,"interface mean translation fit failed");need(std::abs(response.motorRotationRad[j]-rotation[j])<1e-15,"interface angular fit failed");}
    CarrierStructuralResponse failure;failure.maximumInterfaceDisplacementM=25e-6;failure.angularDeflectionRad=.001;
    const auto proposal=proposeMotorCarrierThicknessChange(part,failure);need(proposal&&proposal->nextValue>1.5&&proposal->nextValue<=3,"critic exceeded bounded step");
    part.changeParameter(proposal->parameterId,proposal->nextValue,proposal->engineeringReason);
    need(part.changes.size()==1&&!part.changes.front().engineeringReason.empty(),"critic change lost its engineering reason");
    for(const auto& v:part.variables)if(v.id!="plate.thickness")need(v==candidate.document.engineeringModel().partRequirements.front().variable(v.id),"critic changed a mating interface");
    const auto regenerated=generateMotorCarrier(part);
    need(evaluateMotorCarrierManufacturing(regenerated).geometricScreeningPassed(),"critic candidate failed manufacturing geometry screening");
    need(analysisGeometrySha256(*regenerated.document.object(regenerated.cadObjectId))!=analysisGeometrySha256(*candidate.document.object(candidate.cadObjectId)),"redesign did not change the exact solid");
    for(std::size_t i=0;i<candidate.holes.size();++i){const auto& old=candidate.holes[i];const auto& next=regenerated.holes[i];
        need(old.xMm==next.xMm&&old.yMm==next.yMm&&old.diameterMm==next.diameterMm,"regeneration changed a fixed mating dimension");}
    auto qualification=loadPartRequirements(QString::fromUtf8(DATUMLOOP_CARRIER_STUDY).replace("design-study.json","analysis-study.json"));
    need(qualification.unresolvedCriticalRequirements().size()==4,"application qualification gaps are not explicit unknown requirements");
    const auto conditionalCandidate=generateMotorCarrier(qualification);
    (void)makeMotorCarrierStaticStudy(conditionalCandidate);
    const auto unresolved=qualification.evaluate("conditional","geometry",{{"relative_interface_displacement",1e-6,"m","synthetic-test"},
        {"interface_angular_deflection",1e-5,"rad","synthetic-test"},{"maximum_von_mises_stress",1e6,"Pa","synthetic-test"}});
    need(!unresolved.passed(),"passing static metrics silently resolved application qualification gaps");
    CarrierStructuralResponse pass;pass.maximumInterfaceDisplacementM=4e-6;pass.angularDeflectionRad=1e-4;
    need(!proposeMotorCarrierThicknessChange(part,pass),"critic changed a passing precision candidate");
    part.changeParameter("plate.thickness",5.5,"Test upper design bound");
    need(!proposeMotorCarrierThicknessChange(part,failure),"critic exceeded the thickness domain");
    std::cout<<"Carrier load equivalence, interface metric mathematics and bounded critic passed; synthetic fields are not FEA.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
