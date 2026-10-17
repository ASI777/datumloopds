#include "datumloop/engineering/CarrierStructuralDesign.hpp"
#include "datumloop/core/DatumUuid.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <unordered_set>
namespace datumloop {
namespace {
double norm(const Vector3& a){return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}
Vector3 sub(const Vector3& a,const Vector3& b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Vector3 cross(const Vector3& a,const Vector3& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
const MechanicalInterface& interface(const PartRequirements& p,const char* id){
    const auto i=std::find_if(p.interfaces.begin(),p.interfaces.end(),[&](const auto& x){return x.id==id;});
    if(i==p.interfaces.end())throw std::invalid_argument("missing carrier interface");
    return *i;
}
double value(const PartRequirements& p,const char* id,const char* unit){
    const auto& r=p.requirement(id);if(!r.value)throw std::invalid_argument("missing numeric load requirement");
    return convertRequirementValue(*r.value,r.unit,unit);
}
double density(const MotorCarrierCandidate& c){
    const auto& material=c.document.electronicsModel().materials.front();
    const auto i=std::find_if(material.propertyModels.begin(),material.propertyModels.end(),[](const auto& p){return p.propertyName=="mass.density";});
    if(i==material.propertyModels.end()||!i->constantValue||i->propertyUnit!="kg/m^3")throw std::invalid_argument("missing sourced density");
    return *i->constantValue;
}
Vector3 solve3(double a[3][3],Vector3 b){
    for(int col=0;col<3;++col){
        int pivot=col;for(int row=col+1;row<3;++row)if(std::abs(a[row][col])>std::abs(a[pivot][col]))pivot=row;
        if(std::abs(a[pivot][col])<1e-18)throw std::invalid_argument("degenerate motor rotation fit");
        for(int j=0;j<3;++j)std::swap(a[pivot][j],a[col][j]);
        std::swap(b[pivot],b[col]);
        const double divisor=a[col][col];for(int j=0;j<3;++j)a[col][j]/=divisor;b[col]/=divisor;
        for(int row=0;row<3;++row)if(row!=col){const double factor=a[row][col];for(int j=0;j<3;++j)a[row][j]-=factor*a[col][j];b[row]-=factor*b[col];}
    }return b;
}
}
StructuralAnalysisStudy makeMotorCarrierStaticStudy(const MotorCarrierCandidate& c){
    const auto& p=c.document.engineeringModel().partRequirements.front();p.validate();
    // A defined load case can be computed while broader application requirements
    // remain UNKNOWN. PartRequirements::evaluate keeps overall acceptance false.
    for(const auto* id:{"load.normal.force","machine.design.torque","machine.acceleration","load.gravity"}) {
        const auto& requirement=p.requirement(id);
        if(!requirement.value||!requirement.evidence.reviewed||requirement.status==RequirementStatus::Unknown)
            throw std::invalid_argument("carrier load case input is unknown or unreviewed");
    }
    const auto& motor=interface(p,"motor.mounting");const auto& gantry=interface(p,"gantry.mounting");
    if(motor.topologyReferenceIds.size()!=4||gantry.topologyReferenceIds.size()!=4||c.holes.size()!=8)
        throw std::invalid_argument("carrier recipe interface mapping is incomplete");
    StructuralAnalysisStudy s;s.id=createDatumUuidV4();s.name="Selected Z-spindle envelope: acceleration/gravity + peak reaction torque";
    s.cadObjectId=c.cadObjectId;s.materialDefinitionId=c.document.electronicsModel().materials.front().id;
    s.materialStateId=c.document.electronicsModel().materialStates.front().id;
    s.equationDefinitionId="structural.linear-elasticity.3d.isotropic-small-strain.v1";
    // Only the two M4 bore surfaces are fixed. Dowels remain unloaded/free here.
    for(std::size_t i:{0u,2u}){FixedDisplacement f;f.id=createDatumUuidV4();f.name="Ideal rigid M4 bore restraint; joint compliance unknown";
        f.cadObjectId=c.cadObjectId;f.topologyReferenceId=gantry.topologyReferenceIds[i];s.boundaryConditions.push_back(f);}
    const double force=value(p,"load.normal.force","N"),torque=value(p,"machine.design.torque","N*m");
    const double cx=p.parameterIn("motor.center.x","m"),cy=p.parameterIn("motor.center.y","m");
    for(std::size_t i=0;i<3;++i){
        const auto& hole=c.holes[i+1];const double x=hole.xMm*1e-3-cx,y=hole.yMm*1e-3-cy;
        const double radiusSquared=x*x+y*y;
        if(radiusSquared<=0)throw std::invalid_argument("zero motor bolt lever arm");
        AppliedForce f;f.id=createDatumUuidV4();f.name="load.normal.force/3 + machine.design.torque tangential equivalent";
        f.cadObjectId=c.cadObjectId;f.topologyReferenceId=motor.topologyReferenceIds[i+1];
        f.totalForceN={-torque*y/(3*radiusSquared),torque*x/(3*radiusSquared),force/3};s.boundaryConditions.push_back(f);
    }
    BodyForce self;self.id=createDatumUuidV4();self.name="Carrier own inertia: density*(machine.acceleration + load.gravity), +Z";
    self.cadObjectId=c.cadObjectId;self.forceDensityNPerM3={0,0,density(c)*(value(p,"machine.acceleration","m/s^2")+value(p,"load.gravity","m/s^2"))};
    s.boundaryConditions.push_back(self);s.validate();return s;
}
CarrierStructuralResponse evaluateMotorCarrierResponse(const MotorCarrierCandidate& c,const AnalysisMesh& mesh,const StructuralAnalysisRun& run){
    const auto* object=c.document.object(c.cadObjectId);
    if(!object||!run.converged||mesh.cadObjectId!=c.cadObjectId||run.provenance.cadObjectId!=c.cadObjectId||
       run.provenance.mesh.revisionId!=mesh.provenance.revisionId||run.provenance.sourceProjectId!=c.document.projectId()||
       run.provenance.mesh.geometrySha256!=analysisGeometrySha256(*object))
        throw std::invalid_argument("carrier response must use the actual candidate solver run");
    const auto f=std::find_if(run.fields.begin(),run.fields.end(),[](const auto& x){return x.fieldType==FieldType::Displacement;});
    if(f==run.fields.end()||f->location!=FieldLocation::Nodes||f->components!=std::vector<std::string>{"x","y","z"})
        throw std::invalid_argument("carrier response requires nodal vector displacement");
    f->validate(mesh.nodes.size(),mesh.elementCount(),mesh.provenance.revisionId,run.provenance.solverRunId);
    const auto displacement=[&](std::uint32_t n){return Vector3{f->values[3*n],f->values[3*n+1],f->values[3*n+2]};};
    const auto& p=c.document.engineeringModel().partRequirements.front();const auto& motor=interface(p,"motor.mounting");
    std::map<std::string,int> attributes;for(const auto& r:mesh.boundaryRegions)attributes[r.topologyReferenceId]=r.attribute;
    std::map<int,Vector3> means;std::map<int,double> areas;
    for(const auto& t:mesh.boundaryTriangles){
        const auto a=mesh.nodes[t.nodeIndices[0]].positionM,b=mesh.nodes[t.nodeIndices[1]].positionM,d=mesh.nodes[t.nodeIndices[2]].positionM;
        const double area=.5*norm(cross(sub(b,a),sub(d,a)));areas[t.attribute]+=area;
        for(const auto n:t.nodeIndices){const auto u=displacement(n);for(int j=0;j<3;++j)means[t.attribute][j]+=area*u[j]/3;}
    }
    for(auto& [attribute,u]:means)for(double& x:u)x/=areas.at(attribute);
    const auto& gantry=interface(p,"gantry.mounting");
    if(gantry.topologyReferenceIds.size()!=4)throw std::invalid_argument("missing ideal gantry frame");
    for(const auto index:{0u,2u}) {
        const int attribute=attributes.at(gantry.topologyReferenceIds[index]);
        if(!areas.contains(attribute))throw std::invalid_argument("missing gantry support boundary");
        for(const auto& triangle:mesh.boundaryTriangles)if(triangle.attribute==attribute)
            for(const auto node:triangle.nodeIndices)if(norm(displacement(node))>1e-12)
                throw std::invalid_argument("carrier response requires its zero-motion ideal gantry frame");
    }
    CarrierStructuralResponse response;
    std::unordered_set<int> motorAttributes;for(const auto& id:motor.topologyReferenceIds)motorAttributes.insert(attributes.at(id));
    // The two fully fixed M4 interfaces define the zero-motion ideal gantry frame.
    // Max over all pilot/bolt interface nodes avoids hiding local face compliance.
    for(const auto& t:mesh.boundaryTriangles)if(motorAttributes.contains(t.attribute))for(auto n:t.nodeIndices)
        response.maximumInterfaceDisplacementM=std::max(response.maximumInterfaceDisplacementM,norm(displacement(n)));
    std::array<Vector3,3> centers{},motions{};Vector3 center{};
    for(std::size_t i=0;i<3;++i){centers[i]={c.holes[i+1].xMm*1e-3,c.holes[i+1].yMm*1e-3,0};motions[i]=means.at(attributes.at(motor.topologyReferenceIds[i+1]));
        for(int j=0;j<3;++j){center[j]+=centers[i][j]/3;response.meanMotorDisplacementM[j]+=motions[i][j]/3;}}
    double matrix[3][3]{};Vector3 rhs{};
    for(std::size_t i=0;i<3;++i){const auto r=sub(centers[i],center),u=sub(motions[i],response.meanMotorDisplacementM);const auto b=cross(r,u);
        const double r2=r[0]*r[0]+r[1]*r[1]+r[2]*r[2];for(int row=0;row<3;++row){rhs[row]+=b[row];for(int col=0;col<3;++col)matrix[row][col]+=(row==col?r2:0)-r[row]*r[col];}}
    response.motorRotationRad=solve3(matrix,rhs);response.angularDeflectionRad=norm(response.motorRotationRad);
    const double normal=value(p,"load.normal.force","N");
    if(std::abs(response.meanMotorDisplacementM[2])>1e-30)response.normalStiffnessNPerM=normal/std::abs(response.meanMotorDisplacementM[2]);
    response.nominalMassKg=c.document.object(c.cadObjectId)->metrics.volume*1e-9*density(c);
    response.designMetrics={{"relative_interface_displacement",response.maximumInterfaceDisplacementM,"m",run.provenance.solverRunId},
        {"interface_angular_deflection",response.angularDeflectionRad,"rad",run.provenance.solverRunId},
        {"maximum_von_mises_stress",run.maximumVonMisesStressPa,"Pa",run.provenance.solverRunId}};
    return response;
}
std::optional<DesignParameterChange> proposeMotorCarrierThicknessChange(
    const PartRequirements& part,const CarrierStructuralResponse& response){
    part.validate();
    const auto& variable=part.variable("plate.thickness");
    if(variable.role!=ParameterRole::BoundedVariable||!variable.minimum||!variable.maximum)
        throw std::invalid_argument("carrier redesign requires a bounded thickness variable");
    const double displacementLimit=value(part,"machine.interface.displacement","m");
    const double angleLimit=value(part,"machine.interface.angle","rad");
    if(!std::isfinite(response.maximumInterfaceDisplacementM)||response.maximumInterfaceDisplacementM<0||
       !std::isfinite(response.angularDeflectionRad)||response.angularDeflectionRad<0||displacementLimit<=0||angleLimit<=0)
        throw std::invalid_argument("invalid critic response or precision limits");
    const double ratio=std::max(response.maximumInterfaceDisplacementM/displacementLimit,response.angularDeflectionRad/angleLimit);
    if(ratio<=1)return std::nullopt;
    const double previous=part.parameterIn("plate.thickness","mm");
    const double upper=convertRequirementValue(*variable.maximum,part.requirement(variable.requirementId).unit,"mm");
    // Plate bending suggests inverse-cube compliance. This is only a proposal;
    // increased self inertia and in-plane torsion require a new numerical solve.
    const double proposed=std::ceil(previous*std::cbrt(ratio)*1.1/.25)*.25;
    const double next=std::min({upper,previous+1.5,std::max(previous+.25,proposed)});
    if(next<=previous+1e-12)return std::nullopt;
    return DesignParameterChange{"plate.thickness",variable.value,
        convertRequirementValue(next,"mm",part.requirement(variable.requirementId).unit),
        "MFEM interface displacement="+std::to_string(response.maximumInterfaceDisplacementM*1e6)+
        " um (limit "+std::to_string(displacementLimit*1e6)+"), angle="+
        std::to_string(response.angularDeflectionRad)+" rad (limit "+std::to_string(angleLimit)+
        "); increase plate bending stiffness using an inverse-cube compliance hypothesis, rounded to 0.25 mm and capped at +1.5 mm per step. Preserve every mating diameter and hole axis; regenerate, remesh and re-solve before judging improvement."};
}
} // namespace datumloop
