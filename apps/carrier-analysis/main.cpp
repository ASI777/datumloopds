#include "datumloop/engineering/CarrierStructuralDesign.hpp"
#include "datumloop/engineering/Manufacturing.hpp"
#include "datumloop/io/DldProjectStore.hpp"
#include "datumloop/io/PartRequirementsCodec.hpp"
#include "datumloop/meshing/GmshProcessMesher.hpp"
#include "datumloop/numerics/MfemLinearElasticitySolver.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <chrono>
#include <cmath>
#include <QFileInfo>
#include <iostream>
using namespace datumloop;
namespace {
QJsonArray vector(const Vector3& v){return {v[0],v[1],v[2]};}
void write(const QString& path,const QJsonObject& data){QFile f(path);if(!f.open(QIODevice::WriteOnly|QIODevice::NewOnly))throw std::runtime_error("output already exists or cannot be created");const auto b=QJsonDocument(data).toJson(QJsonDocument::Indented);if(f.write(b)!=b.size())throw std::runtime_error("output write failed");}
double change(double a,double b){return std::abs(a-b)/std::max(std::abs(a),1e-30);}
struct CandidateExecution { CarrierStructuralResponse response;bool converged=false;bool requirementsPassed=false;bool quantitativeConstraintsPassed=false; };
CandidateExecution executeCandidate(PartRequirements part,const QDir& output,const QCommandLineParser& parser){
    if(QFileInfo::exists(output.absolutePath()))throw std::invalid_argument("candidate directory must not already exist");
    if(!QDir().mkpath(output.absolutePath()))throw std::runtime_error("cannot create candidate output");
        auto candidate=generateMotorCarrier(part);
        const auto manufacturing=evaluateMotorCarrierManufacturing(candidate);
        if(!manufacturing.geometricScreeningPassed())throw std::invalid_argument("candidate failed manufacturing geometry screening");
        auto study=makeMotorCarrierStaticStudy(candidate);
        const auto stored=candidate.document.engineeringModel().partRequirements.front();
        write(output.filePath("requirements.json"),encodePartRequirements(stored));
        DldProjectStore::save(candidate.document,output.filePath("source.dld"));
        AnalysisMeshingRequest request{candidate.document.projectId(),candidate.document.revision(),candidate.cadObjectId,{}};
        request.settings.maximumElementAspectRatio=12;request.settings.maximumElementCount=1500000;
        GmshProcessMesher mesher({parser.value("gmsh-python").toStdString(),DATUMLOOP_GMSH_WORKER,300000,4096});
        LinearElasticitySolverSettings settings;
        // Interior web point in SI, away from bolt/pilot and ideal restraint.
        settings.sectionStressSamplePositionM=Vector3{0,-.003,.375*stored.parameterIn("plate.thickness","m")};
        settings.absoluteTolerance=1e-16;settings.relativeTolerance=1e-10;
        MfemLinearElasticitySolver solver;QJsonArray runs;
        StructuralAnalysisRun finest;CarrierStructuralResponse previous,latest;double previousEnergy=0,previousProbe=0;
        bool convergence=false;double previousSize=1e100;
        for(const auto& entry:parser.value("sizes-mm").split(',')){
            bool ok=false;const double size=entry.toDouble(&ok)*1e-3;
            if(!ok||!std::isfinite(size)||size<=0||size>=previousSize)throw std::invalid_argument("mesh sizes must be finite, positive and decreasing");
            previousSize=size;request.settings.maximumElementSizeM=size;
            const auto mesh=mesher.generate(candidate.document,request);
            std::cout<<"Meshed h="<<size<<" m: "<<mesh.elementCount()<<" tetrahedra"<<std::endl;
            const auto start=std::chrono::steady_clock::now();auto run=solver.solve(candidate.document,study,mesh,settings);
            const double solveSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            if(run.relativeForceBalanceError>1e-6)
                throw std::runtime_error("carrier solve failed the 1e-6 relative force-equilibrium gate");
            latest=evaluateMotorCarrierResponse(candidate,mesh,run);
            const auto evaluation=stored.evaluate(run.provenance.solverRunId,mesh.geometrySha256,latest.designMetrics);
            QJsonArray checks;for(const auto& check:evaluation.checks)checks.append(QJsonObject{{"id",QString::fromStdString(check.constraintId)},
                {"status",check.status==DesignCheckStatus::Passed?"PASS":check.status==DesignCheckStatus::Failed?"FAIL":"UNKNOWN"},
                {"measured",check.measuredValue?QJsonValue(*check.measuredValue):QJsonValue(QJsonValue::Null)},{"reason",QString::fromStdString(check.reason)}});
            QJsonObject record{{"solver_run_id",QString::fromStdString(run.provenance.solverRunId)},
                {"mesh_revision_id",QString::fromStdString(mesh.provenance.revisionId)},{"geometry_sha256",QString::fromStdString(mesh.geometrySha256)},
                {"requested_size_m",size},{"nodes",static_cast<double>(mesh.nodes.size())},{"dof",static_cast<double>(3*mesh.nodes.size())},
                {"tetrahedra",static_cast<double>(mesh.elementCount())},{"cad_face_groups",static_cast<int>(mesh.boundaryRegions.size())},
                {"maximum_interface_displacement_m",latest.maximumInterfaceDisplacementM},{"mean_motor_displacement_m",vector(latest.meanMotorDisplacementM)},
                {"motor_rotation_rad",vector(latest.motorRotationRad)},{"angular_deflection_rad",latest.angularDeflectionRad},
                {"motor_force_over_mean_normal_displacement_n_per_m",latest.normalStiffnessNPerM},{"maximum_displacement_m",run.maximumDisplacementM},
                {"maximum_von_mises_stress_pa",run.maximumVonMisesStressPa},{"section_normal_stress_yy_pa",run.sectionAxialStressPa},
                {"section_sample_position_m",vector(run.sectionStressSamplePositionM)},
                {"strain_energy_j",run.strainEnergyJ},{"applied_force_n",vector(run.appliedForceN)},{"reaction_force_n",vector(run.reactionForceN)},
                {"relative_force_balance_error",run.relativeForceBalanceError},{"iterations",static_cast<int>(run.iterations)},
                {"relative_residual",run.relativeResidual},{"absolute_tolerance",settings.absoluteTolerance},
                {"relative_tolerance",settings.relativeTolerance},{"maximum_iterations",static_cast<int>(settings.maximumIterations)},{"meshing_wall_time_s",mesh.meshingWallTimeS},{"solve_and_field_wall_time_s",solveSeconds},{"mesher_peak_memory_bytes",static_cast<double>(mesh.mesherPeakMemoryBytes)},
                {"minimum_vertex_scaled_jacobian",mesh.provenance.minimumScaledJacobian},{"maximum_edge_ratio",mesh.provenance.maximumAspectRatio},
                {"requirements_passed",evaluation.passed()},{"checks",checks}};
            if(!runs.empty()){
                const double u=change(latest.maximumInterfaceDisplacementM,previous.maximumInterfaceDisplacementM),
                    angle=change(latest.angularDeflectionRad,previous.angularDeflectionRad),energy=change(run.strainEnergyJ,previousEnergy),stress=change(run.sectionAxialStressPa,previousProbe);
                record["relative_interface_displacement_change"]=u;record["relative_angle_change"]=angle;
                record["relative_energy_change"]=energy;record["relative_probe_stress_change"]=stress;
                convergence=runs.size()>=2&&u<=.05&&angle<=.05&&energy<=.05&&stress<=.05;
            }
            previous=latest;previousEnergy=run.strainEnergyJ;previousProbe=run.sectionAxialStressPa;
            write(output.filePath(QString("mesh-%1.json").arg(runs.size()+1)),record);runs.append(record);
            std::cout<<"MFEM: interface="<<latest.maximumInterfaceDisplacementM*1e6<<" um; angle="<<latest.angularDeflectionRad
                     <<" rad; energy="<<run.strainEnergyJ<<" J; balance="<<run.relativeForceBalanceError<<"; checks="<<(evaluation.passed()?"PASS":"FAIL")<<std::endl;
            finest=std::move(run);
        }
        if(runs.empty())throw std::invalid_argument("at least one mesh size is required");
        study.runs.push_back(finest);auto engineering=candidate.document.engineeringModel();engineering.structuralStudies.push_back(study);
        candidate.document.setEngineeringModel(std::move(engineering));
        DldProjectStore::save(candidate.document,output.filePath("result.dld"));
        auto reopened=DldProjectStore::load(output.filePath("result.dld"));
        if(reopened.engineeringModel().structuralStudies.back().runs.back()!=finest)throw std::runtime_error("carrier result changed on dld round trip");
        const auto evaluation=stored.evaluate(finest.provenance.solverRunId,finest.provenance.mesh.geometrySha256,latest.designMetrics);
        bool quantitativePass=true;
        for(const auto& check:evaluation.checks) if(check.required && check.status!=DesignCheckStatus::Passed) quantitativePass=false;
        QJsonArray unknowns;for(const auto& id:stored.unresolvedCriticalRequirements())unknowns.append(QString::fromStdString(id));
        write(output.filePath("analysis-report.json"),{{"classification","reference application, idealized linear static CPU analysis; not released"},
            {"project_id",QString::fromStdString(candidate.document.projectId())},{"thickness_mm",stored.parameterIn("plate.thickness","mm")},
            {"nominal_mass_kg",latest.nominalMassKg},{"solver",QString::fromStdString(finest.provenance.solverId)},
            {"mfem_version",QString::fromStdString(finest.provenance.solverVersion)},{"geometry_sha256",QString::fromStdString(finest.provenance.mesh.geometrySha256)},
            {"mesher_build",QString::fromStdString(finest.provenance.mesh.backendBuildInfo)},
            {"load_requirement_ids",QJsonArray{"load.normal.force","machine.design.torque","machine.acceleration","load.gravity","material.designation"}},
            {"manufacturing_geometry_screening",true},{"manufacturing_process_qualified",false},
            {"mesh_converged",convergence},{"mesh_convergence_tolerance",.05},{"runs",runs},
            {"unresolved_critical_requirements",unknowns},{"quantitative_constraints_passed",quantitativePass},
            {"overall_requirements_passed",evaluation.passed()},
            {"limitations",QJsonArray{"Two ideal rigid M4 bore restraints; no bolt preload/contact/joint compliance",
                "Motor bolts receive statically equivalent uniform bore tractions; no rigid motor or pilot contact",
                "Selected quasi-static load envelope; belt pretension/radial load and vibration are not established",
                "Stress peak near ideal constraints may be mesh singular; no yield-based safety factor claimed",
                "Normal stiffness uses motor force only; self inertia is included in the displacement denominator",
                "Nominal geometry/material; tolerance and material uncertainty studies remain unexecuted",
                "Three or more meshes and final relative changes <=5% required for convergence flag"}}});
        std::cout<<"Saved actual carrier fields; mesh convergence="<<(convergence?"PASS":"NOT ESTABLISHED")<<std::endl;
        return {latest,convergence,evaluation.passed(),quantitativePass};
}

}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QCommandLineParser parser;parser.addHelpOption();
    parser.setApplicationDescription("Execute the explicitly selected motor-carrier CPU engineering study");
    parser.addOption({"requirements","Reviewed selected carrier study","file"});
    parser.addOption({"output","New candidate directory","directory"});
    parser.addOption({"gmsh-python","Isolated pinned Gmsh interpreter","path"});
    parser.addOption({"sizes-mm","Decreasing mesh-size list in mm","sizes","1.2,0.8,0.5"});
    parser.addOption({"thickness","Explicit thickness study in mm","mm"});
    parser.addOption({"redesign","Run up to four bounded, converged candidate analyses with the design critic"});parser.process(app);
    try{
        if(!parser.isSet("requirements")||!parser.isSet("output")||!parser.isSet("gmsh-python"))throw std::invalid_argument("requirements, output and gmsh-python are required");
        QDir output(parser.value("output"));
        auto part=loadPartRequirements(parser.value("requirements"));
        if(parser.isSet("thickness")){bool ok=false;const double t=parser.value("thickness").toDouble(&ok);if(!ok)throw std::invalid_argument("invalid thickness");part.changeParameter("plate.thickness",t,"Explicit bounded thickness sensitivity; preserve mating diameters and axes");}
        if(!parser.isSet("redesign")) {
            (void)executeCandidate(part,output,parser);
        } else {
            if(QFileInfo::exists(output.absolutePath()))throw std::invalid_argument("study directory must not already exist");
            if(!QDir().mkpath(output.absolutePath()))throw std::runtime_error("cannot create study directory");
            QJsonArray history;bool improved=false,completed=false;double firstDisplacement=0;
            for(int iteration=0;iteration<4;++iteration){
                const auto result=executeCandidate(part,QDir(output.filePath(QString("candidate-%1").arg(iteration))),parser);
                if(iteration==0)firstDisplacement=result.response.maximumInterfaceDisplacementM;
                history.append(QJsonObject{{"candidate",iteration},{"thickness_mm",part.parameterIn("plate.thickness","mm")},
                    {"interface_displacement_m",result.response.maximumInterfaceDisplacementM},
                    {"angular_deflection_rad",result.response.angularDeflectionRad},{"mesh_converged",result.converged},
                    {"requirements_passed",result.requirementsPassed},{"quantitative_constraints_passed",result.quantitativeConstraintsPassed}});
                if(iteration>0)improved=result.response.maximumInterfaceDisplacementM<firstDisplacement;
                if(!result.converged)break;
                if(result.quantitativeConstraintsPassed){completed=iteration>0&&improved;break;}
                const auto proposal=proposeMotorCarrierThicknessChange(part,result.response);
                if(!proposal||iteration==3)break;
                std::cout<<"Design critic: "<<proposal->engineeringReason<<" Thickness "<<proposal->previousValue<<" -> "<<proposal->nextValue<<" mm"<<std::endl;
                part.changeParameter(proposal->parameterId,proposal->nextValue,proposal->engineeringReason);
            }
            write(output.filePath("design-study-report.json"),{{"candidates",history},{"displacement_improved",improved},
                {"conditional_structural_redesign_demonstrated",completed},{"classification","Conditional static reference study; not a manufacturing release or complete machine qualification"}});
        }
    }catch(const std::exception& e){
        std::cerr<<"Carrier analysis failed: "<<e.what()<<'\n';
        try {
            const QDir output(parser.value("output"));
            if(output.exists())write(output.filePath("failure.json"),{{"completed",false},{"error",QString::fromUtf8(e.what())},
                {"classification","Partial outputs are preserved; no successful analysis or redesign is claimed"}});
        }catch(const std::exception&) {} // Preserve the original failure if storage is unavailable.
        return 1;
    }
}
