#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/engineering/MotorCarrierDesign.hpp"
#include "datumloop/io/PartRequirementsCodec.hpp"
#include "datumloop/io/DldProjectStore.hpp"
#include "datumloop/meshing/GmshProcessMesher.hpp"
#include "datumloop/numerics/MfemLinearElasticitySolver.hpp"
#include "datumloop/ui/AnalysisResultDisplay.hpp"
#include "datumloop/ui/DocumentViewModel.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace datumloop;
namespace {
void need(bool x,const char* why){if(!x)throw std::runtime_error(why);}
void close(double x,double expected,double scale,const char* why){need(std::abs(x-expected)<=scale*2e-7,why);}
template<class F>void rejects(F f,const char* why){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}need(rejected,why);}
const FieldDataset& field(const StructuralAnalysisRun& r,FieldType type){
    const auto f=std::find_if(r.fields.begin(),r.fields.end(),[&](const auto& x){return x.fieldType==type;});
    need(f!=r.fields.end(),"missing solver field");return *f;
}
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    if(std::string(DATUMLOOP_GMSH_PYTHON).empty()){std::cout<<"SKIPPED: optional Gmsh interpreter is not configured\n";return 77;}
    try{
        const auto carrier=generateMotorCarrier(loadPartRequirements(QString::fromUtf8(DATUMLOOP_CARRIER_STUDY)));
        DatumDocument document("Exact uniaxial elasticity patch verification");
        const auto objectId=document.createBox("20 x 10 x 8 mm patch",{20,10,8}).id;
        document.setElectronicsModel(carrier.document.electronicsModel());
        StructuralAnalysisStudy study;
        study.id=createDatumUuidV4();study.name="10 N uniaxial traction, free Poisson contraction";study.cadObjectId=objectId;
        study.materialDefinitionId=document.electronicsModel().materials.front().id;
        study.materialStateId=document.electronicsModel().materialStates.front().id;
        study.equationDefinitionId="structural.linear-elasticity.3d.isotropic-small-strain.v1";
        std::string positiveX;
        for(const auto& ref:document.topologyReferences()){
            if(ref.objectId!=objectId||ref.subshapeType!=DatumSubshapeType::Face)continue;
            for(std::size_t axis=0;axis<3;++axis){
                if(std::abs(ref.geometrySignature.centroidMm[axis]+Vector3{10,5,4}[axis])<1e-10){
                    FixedDisplacement fixed;fixed.id=createDatumUuidV4();fixed.name="Symmetry component on minimum coordinate face";
                    fixed.cadObjectId=objectId;fixed.topologyReferenceId=ref.id;fixed.constrainedComponents={false,false,false};
                    fixed.constrainedComponents[axis]=true;study.boundaryConditions.push_back(fixed);
                }
            }
            if(std::abs(ref.geometrySignature.centroidMm[0]-10)<1e-10)positiveX=ref.id;
        }
        need(study.boundaryConditions.size()==3&&!positiveX.empty(),"patch faces not identified");
        AppliedForce load;load.id=createDatumUuidV4();load.name="10 N x traction";load.cadObjectId=objectId;
        load.topologyReferenceId=positiveX;load.totalForceN={10,0,0};study.boundaryConditions.push_back(load);
        DatumEngineeringModel engineering;engineering.structuralStudies.push_back(study);document.setEngineeringModel(engineering);
        AnalysisMeshingRequest request{document.projectId(),document.revision(),objectId,{}};
        request.settings.maximumElementSizeM=.004;request.settings.maximumElementAspectRatio=12;
        GmshProcessMesher mesher({DATUMLOOP_GMSH_PYTHON,DATUMLOOP_GMSH_WORKER,120000,2048});
        const auto mesh=mesher.generate(document,request);
        LinearElasticitySolverSettings settings;settings.sectionStressSamplePositionM=Vector3{.0013,.0011,.0003};
        MfemLinearElasticitySolver solver;
        const auto run=solver.solve(document,study,mesh,settings);
        need(run.provenance.solverId=="mfem.serial-h1-p1-3d-linear-elasticity","tet run mislabeled as hex solver");
        const double stress=10/(.01*.008),strain=stress/69.6e9,nu=.331;
        const double expectedEnergy=.5*10*strain*.02;
        double maxError=0;
        const auto& u=field(run,FieldType::Displacement).values;
        for(std::size_t i=0;i<mesh.nodes.size();++i){
            const auto p=mesh.nodes[i].positionM;const Vector3 expected{strain*(p[0]+.01),-nu*strain*(p[1]+.005),-nu*strain*(p[2]+.004)};
            for(std::size_t j=0;j<3;++j){close(u[3*i+j],expected[j],strain*.02,"tet nodal displacement differs from exact affine patch");maxError=std::max(maxError,std::abs(u[3*i+j]-expected[j]));}
        }
        const auto& sigma=field(run,FieldType::StressTensor).values;
        const auto& epsilon=field(run,FieldType::StrainTensor).values;
        for(std::size_t i=0;i<mesh.tetrahedra.size();++i)for(std::size_t j=0;j<6;++j){
            close(sigma[6*i+j],j==0?stress:0,stress,"tet stress tensor failed uniaxial patch");
            close(epsilon[6*i+j],j==0?strain:(j<3?-nu*strain:0),strain,"tet strain tensor failed uniaxial patch");
        }
        close(run.strainEnergyJ,expectedEnergy,expectedEnergy,"tet strain energy failed exact patch");
        close(run.sectionAxialStressPa,stress,stress,"explicit interior stress probe failed");
        close(run.reactionForceN[0],-10,10,"tet reaction force does not balance traction");
        need(run.relativeForceBalanceError<1e-8,"tet force equilibrium failed");
        auto hex=OcctPrismAnalysisMesher::generate(document,objectId,{.004,3,250000});
        const auto hexRun=solver.solve(document,study,hex,settings);
        close(run.strainEnergyJ,hexRun.strainEnergyJ,expectedEnergy,"tet/hex strain energies differ on affine patch");
        rejects([&]{(void)solver.solve(document,study,mesh);},"tet run accepted implicit stress probe");
        auto outside=settings;outside.sectionStressSamplePositionM=Vector3{1,1,1};
        rejects([&]{(void)solver.solve(document,study,mesh,outside);},"stress probe outside material accepted");
        QTemporaryDir directory;need(directory.isValid(),"temporary directory unavailable");
        auto persisted=study;persisted.runs.push_back(run);engineering.structuralStudies={persisted};document.setEngineeringModel(engineering);
        const auto path=directory.filePath("patch.dld");DldProjectStore::save(document,path);
        auto reopened=DldProjectStore::load(path);
        const auto& saved=reopened.engineeringModel().structuralStudies.front().runs.front();
        need(saved==run,"tet fields/connectivity/provenance changed during dld round trip");
        need(analysisGeometrySha256(*reopened.object(objectId))==mesh.geometrySha256,"nominal geometry digest changed on dld round trip");
        DocumentViewModel view(&reopened);
        need(view.analysisResultAvailable(),"reopened tetrahedral run is not available in the native view model");
        for(const auto mode:{AnalysisResultField::DisplacementMagnitude,AnalysisResultField::VonMisesStress,AnalysisResultField::EquivalentStrain}){
            const auto display=buildAnalysisResultDisplay(saved,mode,0);
            need(display.mesh.vertices.size()==mesh.boundaryTriangles.size()*3,"tet display exposes internal or missing faces");
        }
        QFile input(path);need(input.open(QIODevice::ReadOnly),"cannot read saved patch");
        auto root=QJsonDocument::fromJson(input.readAll()).object();input.close();root["schemaVersion"]=9;
        QFile old(directory.filePath("unsupported-schema9.dld"));need(old.open(QIODevice::WriteOnly),"cannot write invalid legacy project");
        old.write(QJsonDocument(root).toJson());old.close();
        rejects([&]{(void)DldProjectStore::load(old.fileName());},"tet snapshot accepted under schema nine");
        if(argc==3&&std::string(argv[1])=="--report"){
            QFile report(QString::fromUtf8(argv[2]));need(report.open(QIODevice::WriteOnly),"cannot write patch report");
            const QJsonObject data{{"test","exact_uniaxial_affine_patch"},{"solver",QString::fromStdString(run.provenance.solverId)},
                {"mfem_version",QString::fromStdString(run.provenance.solverVersion)},
                {"tetrahedra",static_cast<double>(mesh.tetrahedra.size())},{"nodes",static_cast<double>(mesh.nodes.size())},
                {"expected_stress_xx_pa",stress},{"expected_strain_xx",strain},{"expected_tip_ux_m",strain*.02},
                {"maximum_nodal_displacement_error_m",maxError},{"expected_strain_energy_j",expectedEnergy},
                {"tet_strain_energy_j",run.strainEnergyJ},{"hex_strain_energy_j",hexRun.strainEnergyJ},
                {"reaction_x_n",run.reactionForceN[0]},{"force_balance_error",run.relativeForceBalanceError},
                {"iterations",static_cast<int>(run.iterations)},{"relative_residual",run.relativeResidual},
                {"dld_round_trip",true},{"tet_fields_displayed",true},{"carrier_fea_status","not_run"}};
            report.write(QJsonDocument(data).toJson(QJsonDocument::Indented));
        }
        std::cout<<"MFEM P1 tetrahedral patch, Q1 comparison, persistence and field display passed; "<<mesh.tetrahedra.size()<<" tetrahedra, max nodal error "<<maxError<<" m\n";
    }catch(const std::exception& e){std::cerr<<"MFEM tetrahedral verification failed: "<<e.what()<<'\n';return 1;}
}
