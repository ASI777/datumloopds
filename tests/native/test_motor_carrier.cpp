#include "datumloop/engineering/MotorCarrierDesign.hpp"
#include "datumloop/engineering/Manufacturing.hpp"
#include "datumloop/io/DldProjectStore.hpp"
#include "datumloop/io/PartRequirementsCodec.hpp"
#include "datumloop/topology/DatumTopologyResolver.hpp"
#include <BRepAdaptor_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool b,const char* why) {if(!b) throw std::runtime_error(why);}
void inspect(const datumloop::MotorCarrierCandidate& candidate,double thickness) {
    const auto* object=candidate.document.object(candidate.cadObjectId);
    require(object && object->metrics.valid && object->featureGraph.outputSucceeded(),"carrier exact solid invalid");
    int solids=0; for(TopExp_Explorer it(object->shape,TopAbs_SOLID);it.More();it.Next()) ++solids;
    require(solids==1,"carrier must remain one exact OCCT solid");
    require(object->featureGraph.features().size()==9 && candidate.holes.size()==8,"carrier lost a semantic feature or hole");
    require(std::abs(object->metrics.maximum[0]-object->metrics.minimum[0]-34)<1e-5 &&
        std::abs(object->metrics.maximum[1]-object->metrics.minimum[1]-44)<1e-5 &&
        std::abs(object->metrics.maximum[2]-object->metrics.minimum[2]-thickness)<1e-5,"carrier dimensions disagree with source selections");
    double removedArea=0;
    for(const auto& hole:candidate.holes) removedArea+=std::acos(-1.0)*hole.diameterMm*hole.diameterMm/4;
    require(std::abs(object->metrics.volume-(34*44-removedArea)*thickness)<1e-5,"exact carrier volume disagrees with independent analytic hole-volume check");
    const auto& part=candidate.document.engineeringModel().partRequirements.front();
    require(part.unresolvedCriticalRequirements().empty(),"selected application requirements remain unresolved");
    std::vector<std::array<double,2>> actualMotorBolts;
    int actualCylinders=0, pilotCount=0, dowelCount=0, gantryBoltCount=0;
    for(const auto& reference:candidate.document.topologyReferences()) {
        if(reference.subshapeType!=datumloop::DatumSubshapeType::Face || reference.geometrySignature.geometryType!="cylinder") continue;
        auto result=datumloop::DatumTopologyResolver::resolve(reference,object->id,object->shape,{},object->featureGraph.outputFeatureId());
        require(result.status==datumloop::DatumTopologyResolutionStatus::Resolved,"persistent bore face cannot be resolved");
        BRepAdaptor_Surface surface(TopoDS::Face(result.subshape));
        const auto cylinder=surface.Cylinder(); ++actualCylinders;
        const auto center=cylinder.Axis().Location();
        if(std::abs(cylinder.Radius()-3.01)<1e-6) {
            ++pilotCount;
            require(std::hypot(center.X(),center.Y()-8)<1e-6,"actual pilot bore is misplaced");
        }
        if(std::abs(cylinder.Radius()-1.5)<1e-6) {
            ++dowelCount;
            require(std::abs(std::abs(center.X())-6)<1e-6 && std::abs(center.Y()+10)<1e-6,"actual precision locating bores are misplaced");
        }
        if(std::abs(cylinder.Radius()-2.25)<1e-6) {
            ++gantryBoltCount;
            require(std::abs(std::abs(center.X())-11)<1e-6 && std::abs(center.Y()+15)<1e-6,"actual gantry mounting holes are misplaced");
        }
        if(std::abs(cylinder.Radius()-1.1)<1e-6) {
            require(std::abs(std::hypot(center.X(),center.Y()-8)-8)<1e-6,"actual M2 hole axis does not lie on the source Ø16 pitch circle");
            actualMotorBolts.push_back({center.X(),center.Y()-8});
        }
    }
    require(actualCylinders==8 && actualMotorBolts.size()==3 && pilotCount==1 && dowelCount==2 && gantryBoltCount==2,
            "exact BRep does not contain all required physical bores");
    require(candidate.document.electronicsModel().materials.size()==1 &&
        candidate.document.electronicsModel().materials.front().propertyModels.size()==3 &&
        candidate.document.electronicsModel().materials.front().sources.front().kind==datumloop::MaterialSourceKind::PublishedSource,
        "carrier lost its sourced 6061-T6 material model");
    for(int i=0;i<3;++i) for(int j=i+1;j<3;++j) {
        const double dot=(actualMotorBolts[i][0]*actualMotorBolts[j][0]+actualMotorBolts[i][1]*actualMotorBolts[j][1])/64;
        require(std::abs(dot+0.5)<1e-10,"actual motor bolt spacing is not 120 degrees");
    }
    for(const auto& interface:part.interfaces) require(interface.topologyReferenceIds.size()==4,"mechanical interface lost persistent bore faces");
    for(const auto& feature:object->featureGraph.features()) require(!feature.name.empty() && !feature.semanticRole.empty() && feature.parameterDefinitions.size()==3,"native semantic feature lost parameter provenance");
}
}
int main(int argc,char* argv[]) {
    QCoreApplication app(argc,argv);
    try {
        auto part=datumloop::loadPartRequirements(QString::fromUtf8(DATUMLOOP_CARRIER_STUDY));
        auto first=datumloop::generateMotorCarrier(part); inspect(first,1.5);
        const auto manufacturing=datumloop::evaluateMotorCarrierManufacturing(first);
        require(manufacturing.geometricScreeningPassed() && manufacturing.features.size()==8,
                "supported carrier recipe failed manufacturing screening");
        auto strict=part;
        auto& wall=*std::find_if(strict.requirements.begin(),strict.requirements.end(),[](const auto& r){return r.id=="manufacturing.wall.min";});
        wall.value=2;
        auto thin=datumloop::generateMotorCarrier(strict);
        const auto failedManufacturing=datumloop::evaluateMotorCarrierManufacturing(thin);
        require(!failedManufacturing.geometricScreeningPassed(),"thin wall violation was treated as manufacturable");
        auto secondRequirements=part;
        secondRequirements.changeParameter("plate.thickness",2.5,"Thickness study to increase bending stiffness; no claimed FEA improvement before solving");
        auto second=datumloop::generateMotorCarrier(secondRequirements); inspect(second,2.5);
        require(first.document.object(first.cadObjectId)->metrics.volume < second.document.object(second.cadObjectId)->metrics.volume,"thickness edit did not regenerate exact geometry");
        auto stale=first.document.engineeringModel(); stale.partRequirements.front().changeParameter("plate.thickness",2.5,"test stale geometry detection");
        bool rejected=false; try {first.document.setEngineeringModel(stale);} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"changed source parameters were attached to stale geometry");
        QTemporaryDir directory;require(directory.isValid(),"temporary directory unavailable");
        const auto path=directory.filePath("carrier.dld");datumloop::DldProjectStore::save(second.document,path);
        auto loaded=datumloop::DldProjectStore::load(path);
        require(loaded.engineeringModel()==second.document.engineeringModel(),"reopen lost interface face UUIDs or selected requirements");
        require(loaded.electronicsModel()==second.document.electronicsModel(),"reopen lost sourced structural material or reference state");
        auto& a=loaded.objects().front().featureGraph.features();auto& b=second.document.objects().front().featureGraph.features();
        require(a.size()==b.size(),"reopen lost editable features");
        for(std::size_t i=0;i<a.size();++i) require(a[i].name==b[i].name && a[i].semanticRole==b[i].semanticRole && a[i].parameterDefinitions==b[i].parameterDefinitions && a[i].parameters==b[i].parameters,"reopen changed semantic graph or parameter provenance");
        auto features=loaded.objects().front().featureGraph.features();
        features.back().parameters["xMm"]=1000;
        datumloop::DatumFeatureGraph invalid(loaded.objects().front().id,features,features.back().id);
        require(!invalid.outputSucceeded(),"through hole outside the stock was silently accepted");
        std::cout<<"Editable motor carrier: 8 actual bores, Ø16/120-degree motor pattern, 2 precision dowel bores, 9 sourced features, thickness regeneration, stale-source rejection and native DLD reopen passed.\n";
    } catch(const std::exception& error) {std::cerr<<"Motor carrier failed: "<<error.what()<<'\n';return 1;}
    return 0;
}
