#include "datumloop/engineering/Manufacturing.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace datumloop {
std::string manufacturingCheckStatusName(ManufacturingCheckStatus s) {
    switch(s) {
    case ManufacturingCheckStatus::Passed:return "PASSED";
    case ManufacturingCheckStatus::Failed:return "FAILED";
    case ManufacturingCheckStatus::NotApplicable:return "NOT_APPLICABLE";
    case ManufacturingCheckStatus::Unknown:return "UNKNOWN";
    }
    throw std::invalid_argument("unknown manufacturing check status");
}
std::string manufacturingProcessName(ManufacturingProcess p) {
    switch(p) {
    case ManufacturingProcess::ThreeAxisCnc:return "3_AXIS_CNC";
    case ManufacturingProcess::Drilling:return "DRILLING";
    case ManufacturingProcess::Reaming:return "REAMING";
    case ManufacturingProcess::Threading:return "THREADING";
    }
    throw std::invalid_argument("unknown manufacturing process");
}
bool ManufacturingEvaluation::geometricScreeningPassed() const noexcept {
    return !checks.empty() && std::all_of(checks.begin(),checks.end(),[](const auto& c) {
        return c.status==ManufacturingCheckStatus::Passed || c.status==ManufacturingCheckStatus::NotApplicable;
    });
}
ManufacturingEvaluation evaluateMotorCarrierManufacturing(const MotorCarrierCandidate& c) {
    const auto* object=c.document.object(c.cadObjectId);
    if(!object || c.document.engineeringModel().partRequirements.size()!=1) throw std::invalid_argument("missing carrier source model");
    const auto& p=c.document.engineeringModel().partRequirements.front();p.validate();
    ManufacturingEvaluation r{c.cadObjectId,c.document.revision(),{},{},{
        "Geometric screening of the supported plate and normal-through-hole recipe; not CAM verification or process qualification.",
        "Toolholders, fixturing forces, cutting forces, burrs, flatness, residual stress and achieved tolerances are not simulated.",
        "Precision-bore checks establish a specified reaming/inspection requirement, not an achieved fit or sub-5-micrometre assembly accuracy."}};
    const auto& features=object->featureGraph.features();
    const bool recipe=object->metrics.valid && features.size()==9 &&
        features.front().type==DatumFeatureType::ExtrudeRectangle &&
        std::all_of(features.begin()+1,features.end(),[](const auto& f){return f.type==DatumFeatureType::ThroughHole;});
    if(!recipe) {
        r.checks.push_back({{"supported_recipe",{},"semantic feature inventory"},ManufacturingCheckStatus::Unknown,
            {},{},"1","Unsupported geometry requires a separate manufacturing analysis"});
        return r;
    }
    const auto layout=motorCarrierHoleLayout(p);
    const double width=p.parameterIn("plate.width","mm"), height=p.parameterIn("plate.height","mm");
    const double thickness=p.parameterIn("plate.thickness","mm");
    const auto limit=[&](const char* key,const char* unit) {
        const auto& requirement=p.requirement(key);
        if(!requirement.value) throw std::invalid_argument("manufacturing limit is unknown");
        return convertRequirementValue(*requirement.value,requirement.unit,unit);
    };
    const auto add=[&](std::string key,std::vector<std::string> requirements,std::string method,
                       double actual,double bound,const char* unit,bool passed,std::string reason) {
        r.checks.push_back({{std::move(key),std::move(requirements),std::move(method)},
            passed?ManufacturingCheckStatus::Passed:ManufacturingCheckStatus::Failed,actual,bound,unit,std::move(reason)});
    };
    const auto& thicknessRequirement=p.requirement("plate.thickness");
    double minimumWall=thickness+convertRequirementValue(thicknessRequirement.lowerDeviation.value_or(0),thicknessRequirement.unit,"mm");
    const double maximumThickness=thickness+convertRequirementValue(thicknessRequirement.upperDeviation.value_or(0),thicknessRequirement.unit,"mm");
    double maximumAspect=0;
    std::vector<double> radii;
    for(std::size_t i=0;i<layout.size();++i) {
        const auto& hole=layout[i]; const auto& feature=features.at(i+1);
        if(feature.name!=hole.name || feature.parameters.at("xMm")!=hole.xMm ||
            feature.parameters.at("yMm")!=hole.yMm || feature.parameters.at("diameterMm")!=hole.diameterMm) {
            throw std::invalid_argument("manufacturing feature data are stale relative to requirements");
        }
        const auto& diameterVariable=p.variable(feature.parameterDefinitions.at("diameterMm").designVariableIds.front());
        const auto& diameterRequirement=p.requirement(diameterVariable.requirementId);
        const double radius=(hole.diameterMm+convertRequirementValue(diameterRequirement.upperDeviation.value_or(0),diameterRequirement.unit,"mm"))*0.5;
        const double minimumDiameter=hole.diameterMm+convertRequirementValue(diameterRequirement.lowerDeviation.value_or(0),diameterRequirement.unit,"mm");
        if(minimumDiameter<=0) throw std::invalid_argument("bore size limits are nonpositive");
        radii.push_back(radius);
        minimumWall=std::min({minimumWall,width*0.5-std::abs(hole.xMm)-radius,height*0.5-std::abs(hole.yMm)-radius});
        maximumAspect=std::max(maximumAspect,maximumThickness/minimumDiameter);
        const bool precision=hole.semanticRole.starts_with("precision.");
        r.features.push_back({feature.id,precision?ManufacturingProcess::Reaming:ManufacturingProcess::Drilling,
            {0,0,1},hole.name});
        if(precision) {
            const bool defined=diameterRequirement.lowerDeviation && diameterRequirement.upperDeviation;
            r.checks.push_back({{"precision_bore_"+std::to_string(i),{diameterRequirement.id},"size-limit and finishing-process specification"},
                defined?ManufacturingCheckStatus::Passed:ManufacturingCheckStatus::Unknown,{}, {},"mm",
                defined?"Ream and inspect the recorded bore limits; accuracy has not been measured":"Precision bore has no specified limits"});
        }
    }
    for(std::size_t i=0;i<layout.size();++i) for(std::size_t j=i+1;j<layout.size();++j)
        minimumWall=std::min(minimumWall,std::hypot(layout[i].xMm-layout[j].xMm,layout[i].yMm-layout[j].yMm)-radii[i]-radii[j]);
    const double wallLimit=limit("manufacturing.wall.min","mm");
    add("minimum_wall",{"manufacturing.wall.min"},"stock thickness, edge ligaments and bore-to-bore ligaments using upper bore size limits",
        minimumWall,wallLimit,"mm",minimumWall>=wallLimit,"Nominal stock with specified dimensional offsets; no cutter-force model");
    const double aspectLimit=limit("manufacturing.hole.aspect.max","1");
    add("hole_depth_diameter",{"manufacturing.hole.aspect.max"},"maximum normal-through-bore depth / diameter",
        maximumAspect,aspectLimit,"1",maximumAspect<=aspectLimit,"Compare with selected shop capability; not a drill-life prediction");
    const double reach=limit("manufacturing.tool.reach","mm");
    add("tool_reach",{"manufacturing.tool.reach"},"plate through-depth versus selected axial reach",
        maximumThickness,reach,"mm",maximumThickness<=reach,"Toolholder interference remains outside this depth check");
    const double penetration=p.parameterIn("screw.penetration","mm");
    const double minimumEngagement=limit("manufacturing.thread.engagement.min","mm");
    const double available=p.parameterIn("motor.mount.thread.depth","mm");
    add("thread_engagement_min",{"screw.penetration","manufacturing.thread.engagement.min"},"selected motor screw penetration",
        penetration,minimumEngagement,"mm",penetration>=minimumEngagement,"Bracket holes are clearance bores; the motor contains the thread");
    add("motor_thread_depth",{"screw.penetration","motor.mount.thread.depth"},"selected screw penetration versus manufacturer thread depth",
        penetration,available,"mm",penetration<=available,"Check screw length for each plate thickness; washer and blind-hole bottom clearances need assembly review");
    add("tool_access",{},"normal-through-hole feature axes",static_cast<double>(layout.size()),static_cast<double>(layout.size()),"1",true,
        "Every bore has a straight +Z approach in this supported recipe; fixture/toolholder collisions are not evaluated");
    add("undercut_detection",{},"restricted feature-family check",0,0,"1",true,"No undercut features in a planar extrusion with complete normal through-bores; arbitrary shapes are not covered");
    add("setup_feasibility",{},"geometric approach-direction plan",2,2,"1",true,"Top drilling/reaming plus reverse-side finishing are geometrically accessible; workholding must still be qualified");
    r.checks.push_back({{"internal_corner_radius",{},"feature inventory"},ManufacturingCheckStatus::NotApplicable,
        {},{},"mm","No internal pocket corners in this recipe; circular bores are drilled or reamed"});
    return r;
}
} // namespace datumloop
