#include "datumloop/engineering/MotorCarrierDesign.hpp"
#include "datumloop/core/DatumUuid.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace datumloop {
namespace {
FeatureParameter binding(const PartRequirements& part, std::vector<std::string> keys, std::string relationship)
{
    FeatureParameter result{"mm", part.id, std::move(keys), std::move(relationship), {}};
    for (const auto& key : result.designVariableIds) result.sourceValues.emplace(key, part.variable(key).value);
    return result;
}
std::map<std::string, FeatureParameter> holeBindings(
    const PartRequirements& p, std::vector<std::string> x, std::vector<std::string> y,
    const std::string& diameter, std::string xRule, std::string yRule)
{
    return {{"xMm", binding(p, std::move(x), std::move(xRule))},
            {"yMm", binding(p, std::move(y), std::move(yRule))},
            {"diameterMm", binding(p, {diameter}, diameter)}};
}
} // namespace

std::vector<MotorCarrierHole> motorCarrierHoleLayout(const PartRequirements& p)
{
    p.validate();
    const auto mm = [&](const char* key) { return p.parameterIn(key, "mm"); };
    const double cx = mm("motor.center.x"), cy = mm("motor.center.y");
    const double count = p.parameterIn("motor.mount.count", "1");
    const double spacing = p.parameterIn("motor.mount.spacing", "rad");
    const double clock = p.parameterIn("motor.clock.angle", "rad");
    if (count != 3 || std::abs(spacing - 2 * std::acos(-1.0) / 3) > 1e-12) {
        throw std::invalid_argument("motor carrier recipe requires the reviewed three-hole 120-degree interface");
    }
    std::vector<MotorCarrierHole> holes;
    holes.push_back({"Motor precision pilot bore", "precision.motor-pilot", "motor.mounting", cx, cy,
        mm("bracket.pilot.fit"), holeBindings(p, {"motor.center.x"}, {"motor.center.y"},
            "bracket.pilot.fit", "motor.center.x", "motor.center.y")});
    for (int i = 0; i < 3; ++i) {
        const double angle = clock + static_cast<double>(i) * spacing;
        const double radius = mm("motor.mount.pcd") * 0.5;
        const std::string index = std::to_string(i);
        holes.push_back({"Motor M2 clearance " + std::to_string(i + 1), "fastener.motor.M2-clearance",
            "motor.mounting", cx + radius * std::cos(angle), cy + radius * std::sin(angle),
            mm("motor.bolt.clearance"), holeBindings(p,
                {"motor.center.x", "motor.mount.pcd", "motor.clock.angle", "motor.mount.spacing", "motor.mount.count"},
                {"motor.center.y", "motor.mount.pcd", "motor.clock.angle", "motor.mount.spacing", "motor.mount.count"},
                "motor.bolt.clearance",
                "motor.center.x + motor.mount.pcd/2*cos(motor.clock.angle + " + index + "*motor.mount.spacing); angles converted to radians",
                "motor.center.y + motor.mount.pcd/2*sin(motor.clock.angle + " + index + "*motor.mount.spacing); angles converted to radians")});
    }
    for (int sign : {-1, 1}) {
        const std::string side = sign < 0 ? "left" : "right";
        holes.push_back({"Gantry M4 clearance " + side, "fastener.gantry.M4-clearance", "gantry.mounting",
            sign * mm("gantry.bolt.spacing") * 0.5, mm("gantry.bolt.y"), mm("gantry.bolt.clearance"),
            holeBindings(p, {"gantry.bolt.spacing"}, {"gantry.bolt.y"}, "gantry.bolt.clearance",
                std::to_string(sign) + "*gantry.bolt.spacing/2", "gantry.bolt.y")});
        holes.push_back({"Gantry precision dowel bore " + side, "precision.gantry-dowel", "gantry.mounting",
            sign * mm("gantry.dowel.spacing") * 0.5, mm("gantry.dowel.y"), mm("gantry.dowel.bore"),
            holeBindings(p, {"gantry.dowel.spacing"}, {"gantry.dowel.y"}, "gantry.dowel.bore",
                std::to_string(sign) + "*gantry.dowel.spacing/2", "gantry.dowel.y")});
    }
    return holes;
}

MotorCarrierCandidate generateMotorCarrier(PartRequirements part)
{
    if (part.requirement("material.designation").textValue != "Aluminum alloy 6061-T6") {
        throw std::invalid_argument("carrier reference recipe requires its explicitly selected 6061-T6 material");
    }
    auto holes = motorCarrierHoleLayout(part);
    const double width = part.parameterIn("plate.width", "mm");
    const double height = part.parameterIn("plate.height", "mm");
    const double thickness = part.parameterIn("plate.thickness", "mm");
    if (width <= 0 || height <= 0 || thickness <= 0) throw std::invalid_argument("invalid carrier stock dimensions");
    DatumDocument document("Z-spindle ECX FLAT 22 L motor carrier");
    const std::string objectId = document.createBox("Z-spindle motor carrier", {width, height, thickness}).id;
    std::vector<DatumFeature> features;
    DatumFeature extrusion;
    extrusion.id = createDatumUuidV4(); extrusion.ownerObjectId = objectId;
    extrusion.type = DatumFeatureType::ExtrudeRectangle;
    extrusion.name = "Rectangular stock profile extrusion";
    extrusion.semanticRole = "stock.carrier-plate";
    extrusion.parameters = {{"xMm", width}, {"yMm", height}, {"zMm", thickness}};
    extrusion.parameterDefinitions = {{"xMm", binding(part, {"plate.width"}, "plate.width")},
        {"yMm", binding(part, {"plate.height"}, "plate.height")},
        {"zMm", binding(part, {"plate.thickness"}, "plate.thickness")}};
    features.push_back(std::move(extrusion));
    for (const auto& hole : holes) {
        DatumFeature feature;
        feature.id = createDatumUuidV4(); feature.ownerObjectId = objectId;
        feature.type = DatumFeatureType::ThroughHole; feature.name = hole.name; feature.semanticRole = hole.semanticRole;
        feature.inputFeatureIds = {features.back().id};
        feature.parameters = {{"xMm", hole.xMm}, {"yMm", hole.yMm}, {"diameterMm", hole.diameterMm}};
        feature.parameterDefinitions = hole.parameterDefinitions;
        features.push_back(std::move(feature));
    }
    const std::string outputId = features.back().id;
    DatumFeatureGraph graph(objectId, std::move(features), outputId);
    if (!graph.outputSucceeded()) throw std::runtime_error("motor carrier feature graph failed to generate exact geometry");
    document.setFeatureGraph(objectId, std::move(graph));
    document.setProperty(objectId, "part.recipe", std::string("maxon.ecx-flat22l.motor-carrier.v1"));
    document.setProperty(objectId, "part.requirementsId", part.id);
    document.setProperty(objectId, "part.motor-mounting-plane-z-mm", thickness * 0.5);

    // Reuse the published reference material already verified by the existing
    // CAD/MFEM slice. Unknown temperature applicability remains unknown.
    MaterialDefinition material;
    material.id = createDatumUuidV4();
    material.name = "Aluminum alloy 6061-T6 (NIST compiled properties)";
    MaterialSource source;
    source.id = createDatumUuidV4(); source.kind = MaterialSourceKind::PublishedSource;
    source.title = "Physical Properties Data Compilations Relevant to Energy Storage. V. Mechanical Properties Data on Alloys for Use in Flywheels";
    source.publisher = "National Bureau of Standards / NIST archive";
    source.version = "NBS NSRDS 61 Part V (1982)";
    source.reference = "H. M. Ledbetter (1982), Table 2(SI), mean values and percent uncertainty; reused from the verified DatumLoop material record";
    source.url = "https://srd.nist.gov/NSRDS/NSRDS-NBS-61-V.pdf";
    material.sources.push_back(source);
    const auto property = [&](std::string name, std::string unit, double value, double fraction) {
        MaterialProperty p;
        p.propertyName = std::move(name); p.propertyUnit = std::move(unit);
        p.law = MaterialPropertyLaw::Constant; p.constantValue = value;
        p.absoluteUncertainty = value * fraction;
        p.source = "NBS NSRDS 61 Part V, Table 2(SI), mean value and reported percent uncertainty";
        p.sourceVersion = source.version; p.sourceId = source.id;
        return p;
    };
    material.propertyModels = {property("mass.density", "kg/m^3", 2710, 0.002),
        property("elastic.youngs_modulus", "Pa", 69.6e9, 0.027),
        property("elastic.poisson_ratio", "1", 0.331, 0.015)};
    MaterialState state;
    state.id = createDatumUuidV4(); state.materialDefinitionId = material.id; state.temperatureK = 293.15;
    state.provenance = {"Selected reference temperature for this numerical design study; the cited compilation does not establish a verified property temperature range."};
    DatumElectronicsModel materialCatalog;
    materialCatalog.materials.push_back(material); materialCatalog.materialStates.push_back(state);
    document.setElectronicsModel(std::move(materialCatalog));
    document.setProperty(objectId, "part.materialDefinitionId", material.id);

    for (auto& interface : part.interfaces) interface.topologyReferenceIds.clear();
    for (const auto& hole : holes) {
        const auto interface = std::find_if(part.interfaces.begin(), part.interfaces.end(),
            [&](const auto& candidate) { return candidate.id == hole.interfaceId; });
        if (interface == part.interfaces.end()) throw std::invalid_argument("missing carrier mechanical interface definition");
        std::vector<const DatumTopologyReference*> matches;
        for (const auto& face : document.topologyReferences()) {
            if (face.objectId != objectId || face.subshapeType != DatumSubshapeType::Face ||
                face.geometrySignature.geometryType != "cylinder") continue;
            const auto& center = face.geometrySignature.centroidMm;
            const double expectedArea = std::acos(-1.0) * hole.diameterMm * thickness;
            if (std::hypot(center[0] - hole.xMm, center[1] - hole.yMm) < 1e-6 &&
                std::abs(face.geometrySignature.measure - expectedArea) < expectedArea * 1e-7) matches.push_back(&face);
        }
        if (matches.size() != 1) throw std::runtime_error("carrier bore did not map uniquely to persistent CAD face topology");
        interface->topologyReferenceIds.push_back(matches.front()->id);
    }
    DatumEngineeringModel engineering;
    engineering.partRequirements.push_back(std::move(part));
    document.setEngineeringModel(std::move(engineering));
    return {std::move(document), objectId, std::move(holes)};
}
} // namespace datumloop
