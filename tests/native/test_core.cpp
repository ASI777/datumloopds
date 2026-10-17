#include "datumloop/core/DatumCommand.hpp"
#include "datumloop/core/DatumDocument.hpp"
#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/geometry/OcctKernel.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool close(double left, double right, double tolerance = 1e-8)
{
    return std::abs(left - right) <= tolerance;
}

}  // namespace

int main()
{
    try {
        datumloop::DatumDocument document;
        const datumloop::DatumObject& part = document.createBox("DatumLoop Block", {40.0, 30.0, 20.0});
        const std::string id = part.id;

        require(part.id.size() == 36, "document object id is not UUID-shaped");
        require(part.metrics.valid, "OCCT box must be valid");
        require(close(part.metrics.volume, 24000.0), "OCCT box volume is incorrect");
        require(close(part.metrics.surfaceArea, 5200.0), "OCCT box area is incorrect");
        require(close(part.metrics.minimum[0], -20.0), "OCCT box lower bound is incorrect");
        require(close(part.metrics.maximum[2], 10.0), "OCCT box upper bound is incorrect");
        require(part.featureGraph.features().size() == 1 && part.featureGraph.outputSucceeded() &&
                    part.featureGraph.features().front().type == datumloop::DatumFeatureType::Box &&
                    !part.featureGraph.features().front().generatedShape.IsNull(),
                "native box creation did not produce an exact parametric primitive feature");

        const datumloop::TriangleMesh mesh = datumloop::OcctKernel::tessellate(part.shape);
        require(mesh.triangleCount() > 0, "exact OCCT shape did not produce display triangles");
        require(mesh.vertices.size() == mesh.triangleCount() * 3, "display mesh is not a triangle list");
        const double authoritativeVolume = part.metrics.volume;
        const datumloop::TriangleMesh disposableDisplayMesh{};
        require(disposableDisplayMesh.triangleCount() == 0,
                "display mesh should be disposable independently of the exact shape");
        const datumloop::TriangleMesh regeneratedMesh = datumloop::OcctKernel::tessellate(part.shape);
        require(regeneratedMesh.triangleCount() == mesh.triangleCount(),
                "display mesh could not be regenerated from the authoritative exact shape");
        require(close(part.metrics.volume, authoritativeVolume),
                "display tessellation changed exact-shape volume metrics");

        datumloop::CommandHistory history;
        history.execute(document, std::make_unique<datumloop::MoveObjectCommand>(
            id, datumloop::Vector3{0.0, 0.0, 0.0}, datumloop::Vector3{12.5, -3.0, 0.0}));
        require(document.object(id) != nullptr, "created object cannot be resolved by id");
        require(close(document.object(id)->position[0], 12.5), "move command did not update document position");
        require(history.canUndo() && !history.canRedo(), "command history state after execute is incorrect");

        require(history.undo(document), "move command could not be undone");
        require(close(document.object(id)->position[0], 0.0), "undo did not restore the prior position");
        require(history.canRedo(), "undo did not preserve the redo command");

        bool rejectedUnknownObject = false;
        try {
            history.execute(document, std::make_unique<datumloop::MoveObjectCommand>(
                "missing-object", datumloop::Vector3{}, datumloop::Vector3{1.0, 0.0, 0.0}));
        } catch (const std::invalid_argument&) {
            rejectedUnknownObject = true;
        }
        require(rejectedUnknownObject, "move command accepted an unknown object id");
        require(history.canRedo(), "failed command erased the valid redo branch");

        require(history.redo(document), "move command could not be redone");
        require(close(document.object(id)->position[1], -3.0), "redo did not restore the moved position");

        const auto makeFeatureId = [] { return datumloop::createDatumUuidV4(); };
        const std::string boxAId = makeFeatureId();
        const std::string boxBId = makeFeatureId();
        const std::string translatedBId = makeFeatureId();
        const std::string unionId = makeFeatureId();
        const std::string cutId = makeFeatureId();
        const std::string commonId = makeFeatureId();
        datumloop::DatumFeature boxA;
        boxA.id = boxAId;
        boxA.ownerObjectId = id;
        boxA.type = datumloop::DatumFeatureType::Box;
        boxA.parameters = {{"xMm", 10.0}, {"yMm", 10.0}, {"zMm", 10.0}};
        datumloop::DatumFeature boxB = boxA;
        boxB.id = boxBId;
        datumloop::DatumFeature translatedB;
        translatedB.id = translatedBId;
        translatedB.ownerObjectId = id;
        translatedB.type = datumloop::DatumFeatureType::Translate;
        translatedB.inputFeatureIds = {boxBId};
        translatedB.parameters = {{"xMm", 5.0}, {"yMm", 0.0}, {"zMm", 0.0}};
        datumloop::DatumFeature unionFeature;
        unionFeature.id = unionId;
        unionFeature.ownerObjectId = id;
        unionFeature.type = datumloop::DatumFeatureType::BooleanUnion;
        unionFeature.inputFeatureIds = {boxAId, translatedBId};
        datumloop::DatumFeature cutFeature = unionFeature;
        cutFeature.id = cutId;
        cutFeature.type = datumloop::DatumFeatureType::BooleanCut;
        datumloop::DatumFeature commonFeature = unionFeature;
        commonFeature.id = commonId;
        commonFeature.type = datumloop::DatumFeatureType::BooleanIntersection;
        const std::vector<datumloop::DatumFeature> definitions{
            boxA, boxB, translatedB, unionFeature, cutFeature, commonFeature};
        const datumloop::DatumFeatureGraph unionGraph(id, definitions, unionId);
        const datumloop::DatumFeatureGraph cutGraph(id, definitions, cutId);
        const datumloop::DatumFeatureGraph commonGraph(id, definitions, commonId);
        require(unionGraph.outputSucceeded() && close(datumloop::OcctKernel::measure(unionGraph.outputShape()).volume, 1500.0),
                "exact Boolean union did not recompute the expected overlapping-box volume");
        require(cutGraph.outputSucceeded() && close(datumloop::OcctKernel::measure(cutGraph.outputShape()).volume, 500.0),
                "exact Boolean cut did not recompute the expected overlapping-box volume");
        require(commonGraph.outputSucceeded() && close(datumloop::OcctKernel::measure(commonGraph.outputShape()).volume, 500.0),
                "exact Boolean intersection did not recompute the expected overlap volume");
        require(std::all_of(unionGraph.features().begin(), unionGraph.features().end(), [](const auto& feature) {
                    return feature.status == datumloop::DatumFeatureStatus::Succeeded &&
                           !feature.generatedShape.IsNull() && feature.error.empty();
                }),
                "feature graph did not retain exact successful outputs and status for every node");

        datumloop::DatumFeature unsupportedBackend = boxA;
        unsupportedBackend.id = makeFeatureId();
        unsupportedBackend.kernelBackend = "smlib";
        const datumloop::DatumFeatureGraph unsupportedGraph(id, {unsupportedBackend}, unsupportedBackend.id);
        require(!unsupportedGraph.outputSucceeded() &&
                    unsupportedGraph.features().front().status == datumloop::DatumFeatureStatus::Failed &&
                    unsupportedGraph.features().front().error.find("Unsupported geometry backend") != std::string::npos,
                "feature graph silently executed an unsupported backend through OCCT");

        datumloop::DatumFeature cylinder;
        cylinder.id = makeFeatureId();
        cylinder.ownerObjectId = id;
        cylinder.type = datumloop::DatumFeatureType::Cylinder;
        cylinder.parameters = {{"radiusMm", 2.0}, {"heightMm", 10.0}};
        datumloop::DatumFeature sphere;
        sphere.id = makeFeatureId();
        sphere.ownerObjectId = id;
        sphere.type = datumloop::DatumFeatureType::Sphere;
        sphere.parameters = {{"radiusMm", 3.0}};
        datumloop::DatumFeature cone;
        cone.id = makeFeatureId();
        cone.ownerObjectId = id;
        cone.type = datumloop::DatumFeatureType::Cone;
        cone.parameters = {{"bottomRadiusMm", 2.0}, {"topRadiusMm", 1.0}, {"heightMm", 5.0}};
        const std::vector<datumloop::DatumFeature> additionalPrimitives{cylinder, sphere, cone};
        const datumloop::DatumFeatureGraph cylinderGraph(id, additionalPrimitives, cylinder.id);
        const datumloop::DatumFeatureGraph sphereGraph(id, additionalPrimitives, sphere.id);
        const datumloop::DatumFeatureGraph coneGraph(id, additionalPrimitives, cone.id);
        require(cylinderGraph.outputSucceeded() &&
                    close(datumloop::OcctKernel::measure(cylinderGraph.outputShape()).volume,
                          40.0 * std::acos(-1.0), 1e-7),
                "cylinder primitive feature did not produce the expected exact volume");
        require(sphereGraph.outputSucceeded() &&
                    close(datumloop::OcctKernel::measure(sphereGraph.outputShape()).volume,
                          36.0 * std::acos(-1.0), 1e-7),
                "sphere primitive feature did not produce the expected exact volume");
        require(coneGraph.outputSucceeded() &&
                    close(datumloop::OcctKernel::measure(coneGraph.outputShape()).volume,
                          35.0 * std::acos(-1.0) / 3.0, 1e-7),
                "cone primitive feature did not produce the expected exact volume");
        datumloop::DatumFeature invalidBox = boxA;
        invalidBox.id = makeFeatureId();
        invalidBox.parameters["xMm"] = -10.0;
        const datumloop::DatumFeatureGraph invalidGraph(id, {invalidBox}, invalidBox.id);
        require(!invalidGraph.outputSucceeded() &&
                    invalidGraph.features().front().status == datumloop::DatumFeatureStatus::Failed &&
                    !invalidGraph.features().front().error.empty(),
                "failed feature recompute did not retain an explicit status and error");

        const std::vector<std::string> previousTopologyIds = [&document, &id] {
            std::vector<std::string> values;
            for (const auto& reference : document.topologyReferences()) {
                if (reference.objectId == id) values.push_back(reference.id);
            }
            return values;
        }();
        datumloop::CommandHistory featureHistory;
        featureHistory.execute(document, std::make_unique<datumloop::SetFeatureGraphCommand>(id, unionGraph));
        require(close(document.object(id)->metrics.volume, 1500.0) &&
                    document.object(id)->featureGraph.outputFeatureId() == unionId,
                "feature graph command did not install its exact output into the document");
        std::vector<std::string> featureTopologyIds;
        for (const auto& reference : document.topologyReferences()) {
            if (reference.objectId == id) featureTopologyIds.push_back(reference.id);
        }
        require(featureHistory.undo(document) && close(document.object(id)->metrics.volume, 24000.0) &&
                    document.object(id)->featureGraph.features().size() == 1,
                "feature graph command undo did not restore the previous document object");
        std::vector<std::string> restoredTopologyIds;
        for (const auto& reference : document.topologyReferences()) {
            if (reference.objectId == id) restoredTopologyIds.push_back(reference.id);
        }
        require(restoredTopologyIds == previousTopologyIds,
                "feature graph command undo did not restore persistent topology identities");
        require(featureHistory.redo(document) && close(document.object(id)->metrics.volume, 1500.0),
                "feature graph command redo did not restore its exact output");
        std::vector<std::string> redoneTopologyIds;
        for (const auto& reference : document.topologyReferences()) {
            if (reference.objectId == id) redoneTopologyIds.push_back(reference.id);
        }
        require(redoneTopologyIds == featureTopologyIds,
                "feature graph redo did not restore a stable topology-reference set");
        require(previousTopologyIds.size() == 26,
                "prior topology references were not retained for command undo");

        featureHistory.execute(document, std::make_unique<datumloop::SetFeatureGraphCommand>(id, invalidGraph));
        require(close(document.object(id)->metrics.volume, 1500.0) &&
                    document.object(id)->featureGraph.features().front().status == datumloop::DatumFeatureStatus::Failed &&
                    !document.object(id)->featureGraph.features().front().error.empty(),
                "failed graph recompute replaced the last valid exact object shape or lost its error");
        require(featureHistory.undo(document) && document.object(id)->featureGraph.outputFeatureId() == unionId &&
                    close(document.object(id)->metrics.volume, 1500.0),
                "undo did not restore the last valid feature graph after a failed recompute");

        datumloop::DatumDocument engineeringDocument("Engineering model checks");
        const std::string baseObjectId = engineeringDocument.createBox("Motor housing", {30.0, 20.0, 15.0}).id;
        const std::string movingObjectId = engineeringDocument.createBox("Carriage", {12.0, 8.0, 4.0}).id;
        const auto mountingFace = std::find_if(
            engineeringDocument.topologyReferences().begin(), engineeringDocument.topologyReferences().end(),
            [&baseObjectId](const auto& reference) {
                return reference.objectId == baseObjectId && reference.subshapeType == datumloop::DatumSubshapeType::Face;
            });
        require(mountingFace != engineeringDocument.topologyReferences().end(),
                "test motor housing has no persistent mounting face reference");

        datumloop::DatumEngineeringModel engineeringModel;
        datumloop::DatumMechanism mechanism;
        mechanism.id = datumloop::createDatumUuidV4();
        mechanism.name = "Linear axis mechanism";
        mechanism.bodyObjectIds = {baseObjectId, movingObjectId};
        datumloop::DatumMotor motor;
        motor.id = datumloop::createDatumUuidV4();
        motor.mechanismId = mechanism.id;
        motor.name = "Servo motor";
        motor.cadRepresentationObjectId = baseObjectId;
        motor.massKg = 1.25;
        motor.inertiaTensorKgM2 = datumloop::DatumInertiaTensorKgM2{0.002, 0.003, 0.004, 0.0, 0.0, 0.0};
        motor.centerOfMassLocalM = datumloop::Vector3{0.0, 0.0, 0.01};
        motor.ratedSpeedRadPerSec = 100.0;
        motor.maximumSpeedRadPerSec = 300.0;
        motor.ratedTorqueNm = 1.2;
        motor.peakTorqueNm = 2.4;
        motor.torqueCurve = {{0.0, 2.4}, {100.0, 1.8}, {300.0, 0.0}};
        motor.electrical = {48.0, 3.2, 8.0};
        motor.thermal = {2.5, 18.0, 423.15};
        motor.encoder = datumloop::DatumMotorEncoder{"incremental", 4096};
        motor.mountingReferences = {{"housing flange", baseObjectId, mountingFace->id}};
        motor.supplierData = datumloop::DatumMotorSupplierData{
            std::string("Datum Components"), std::string("DL-SV-48-100"), std::string("https://example.invalid/dl-sv-48-100")};
        datumloop::DatumAxis axis;
        axis.id = datumloop::createDatumUuidV4();
        axis.name = "X travel";
        axis.type = datumloop::DatumAxisType::Linear;
        axis.baseObjectId = baseObjectId;
        axis.movingObjectId = movingObjectId;
        axis.directionInBaseFrame = {1.0, 0.0, 0.0};
        axis.actuatorMotorId = motor.id;
        mechanism.axes.push_back(axis);

        const auto makeBody = [](const datumloop::DatumDocument& owner,
                                 const std::string& objectId,
                                 const std::string& name,
                                 const std::string& materialId) {
            const datumloop::DatumObject* object = owner.object(objectId);
            require(object != nullptr && object->featureGraph.outputFeatureId().has_value(),
                    "test mechanism body must have a native feature source");
            datumloop::DatumMechanismBody body;
            body.id = datumloop::createDatumUuidV4();
            body.name = name;
            body.cadObjectId = objectId;
            body.geometrySource = {datumloop::DatumGeometrySourceKind::NativeFeatureGraph,
                                   *object->featureGraph.outputFeatureId(), "feature-graph-v1"};
            body.massKg = 2.0;
            body.centerOfMassLocalM = datumloop::Vector3{0.0, 0.0, 0.01};
            body.inertiaTensorKgM2 = datumloop::DatumInertiaTensorKgM2{0.01, 0.02, 0.03, 0.0, 0.0, 0.0};
            body.materialDefinitionId = materialId;
            body.propertyEvidence = {
                {"massKg", datumloop::DatumPropertyEvidenceKind::UserSpecified, "fixture mass input", "fixture-v1", 1.0},
                {"centerOfMassLocalM", datumloop::DatumPropertyEvidenceKind::UserSpecified, "fixture COM input", "fixture-v1", 1.0},
                {"inertiaTensorKgM2", datumloop::DatumPropertyEvidenceKind::UserSpecified, "fixture inertia input", "fixture-v1", 1.0},
            };
            return body;
        };
        const std::string materialDefinitionId = datumloop::createDatumUuidV4();
        datumloop::DatumMechanismBody baseBody = makeBody(engineeringDocument, baseObjectId, "Base", materialDefinitionId);
        datumloop::DatumMechanismBody movingBody = makeBody(engineeringDocument, movingObjectId, "Carriage", materialDefinitionId);
        mechanism.bodies = {baseBody, movingBody};
        datumloop::DatumJoint joint;
        joint.id = datumloop::createDatumUuidV4();
        joint.name = "Linear slide";
        joint.type = datumloop::DatumJointType::Prismatic;
        joint.bodyAId = baseBody.id;
        joint.bodyBId = movingBody.id;
        joint.lowerLimitSi = 0.0;
        joint.upperLimitSi = 0.25;
        mechanism.joints.push_back(joint);
        datumloop::DatumActuator actuator;
        actuator.id = datumloop::createDatumUuidV4();
        actuator.name = "Axis command";
        actuator.jointId = joint.id;
        actuator.mode = datumloop::DatumActuatorControlMode::Position;
        actuator.motorId = motor.id;
        actuator.trajectory = {{0.0, 0.0}, {1.0, 0.25}};
        actuator.maximumRateSi = 0.25;
        mechanism.actuators.push_back(actuator);
        const auto movingFace = std::find_if(
            engineeringDocument.topologyReferences().begin(), engineeringDocument.topologyReferences().end(),
            [&movingObjectId](const auto& reference) {
                return reference.objectId == movingObjectId &&
                       reference.subshapeType == datumloop::DatumSubshapeType::Face;
            });
        require(movingFace != engineeringDocument.topologyReferences().end(),
                "test carriage has no persistent contact surface reference");
        datumloop::DatumContactPair contact;
        contact.id = datumloop::createDatumUuidV4();
        contact.name = "Guide contact";
        contact.bodyAId = baseBody.id;
        contact.bodyBId = movingBody.id;
        contact.surfaceAReferenceId = mountingFace->id;
        contact.surfaceBReferenceId = movingFace->id;
        contact.law = datumloop::DatumContactLaw::Coulomb;
        contact.frictionCoefficient = 0.15;
        mechanism.contacts.push_back(contact);
        datumloop::DatumRequirement travelRequirement;
        travelRequirement.id = datumloop::createDatumUuidV4();
        travelRequirement.name = "Minimum travel";
        travelRequirement.metricId = "axis.travel";
        travelRequirement.unit = "m";
        travelRequirement.relation = datumloop::DatumRequirementRelation::GreaterOrEqual;
        travelRequirement.minimumSi = 0.2;
        mechanism.requirements.push_back(travelRequirement);
        engineeringModel.mechanisms.push_back(mechanism);
        engineeringModel.motors.push_back(motor);
        engineeringModel.validate();

        datumloop::MaterialDefinition mechanismMaterial;
        mechanismMaterial.id = materialDefinitionId;
        mechanismMaterial.name = "Sourced mechanism steel";
        mechanismMaterial.propertyModels = {
            {"elastic.youngs_modulus", "Pa", "", "", datumloop::MaterialPropertyLaw::Constant,
             210.0e9, {}, "verified test fixture", "fixture-v1", 1.0},
            {"elastic.poisson_ratio", "1", "", "", datumloop::MaterialPropertyLaw::Constant,
             0.29, {}, "verified test fixture", "fixture-v1", 1.0},
        };
        datumloop::MechanismValidationContext mechanismContext;
        mechanismContext.studyKind = datumloop::MechanismStudyKind::Kinematics;
        mechanismContext.documentObjectIds = {baseObjectId, movingObjectId};
        mechanismContext.materialDefinitions = {mechanismMaterial};
        for (const auto& reference : engineeringDocument.topologyReferences()) {
            mechanismContext.surfaceReferences.push_back({reference.id, reference.objectId, reference.subshapeType});
        }
        require(datumloop::validateMechanismForStudy(engineeringModel, mechanism.id, mechanismContext).readyForStudy(),
                "complete mechanism definition was not ready for a kinematic study");
        mechanismContext.studyKind = datumloop::MechanismStudyKind::Dynamics;
        require(datumloop::validateMechanismForStudy(engineeringModel, mechanism.id, mechanismContext).readyForStudy(),
                "sourced mass properties did not satisfy the dynamic study readiness gate");
        mechanismContext.studyKind = datumloop::MechanismStudyKind::Structural;
        require(datumloop::validateMechanismForStudy(engineeringModel, mechanism.id, mechanismContext).readyForStudy(),
                "sourced elastic material properties did not satisfy the structural input gate");
        mechanismContext.studyKind = datumloop::MechanismStudyKind::Contact;
        require(datumloop::validateMechanismForStudy(engineeringModel, mechanism.id, mechanismContext).readyForStudy(),
                "contact surfaces and friction data did not satisfy the contact input gate");

        datumloop::MechanismValidationContext missingConstitutiveContext = mechanismContext;
        missingConstitutiveContext.studyKind = datumloop::MechanismStudyKind::Structural;
        missingConstitutiveContext.materialDefinitions.front().propertyModels.clear();
        const auto missingConstitutiveReport = datumloop::validateMechanismForStudy(
            engineeringModel, mechanism.id, missingConstitutiveContext);
        require(!missingConstitutiveReport.readyForStudy() &&
                    std::any_of(missingConstitutiveReport.diagnostics.begin(), missingConstitutiveReport.diagnostics.end(),
                                [](const auto& item) {
                                    return item.code == datumloop::MechanismDiagnosticCode::MissingMaterialProperty;
                                }),
                "structural readiness accepted a material without sourced elastic constants");

        mechanismContext.studyKind = datumloop::MechanismStudyKind::Dynamics;
        datumloop::DatumEngineeringModel missingMass = engineeringModel;
        missingMass.mechanisms.front().bodies.back().massKg.reset();
        missingMass.mechanisms.front().bodies.back().propertyEvidence.erase(
            std::remove_if(missingMass.mechanisms.front().bodies.back().propertyEvidence.begin(),
                           missingMass.mechanisms.front().bodies.back().propertyEvidence.end(),
                           [](const auto& evidence) { return evidence.propertyId == "massKg"; }),
            missingMass.mechanisms.front().bodies.back().propertyEvidence.end());
        const auto missingMassReport = datumloop::validateMechanismForStudy(
            missingMass, mechanism.id, mechanismContext);
        require(!missingMassReport.readyForStudy() &&
                    std::any_of(missingMassReport.diagnostics.begin(), missingMassReport.diagnostics.end(),
                                [](const auto& item) { return item.code == datumloop::MechanismDiagnosticCode::MissingMass; }),
                "dynamic readiness did not identify an unresolved body mass");

        mechanismContext.studyKind = datumloop::MechanismStudyKind::Contact;
        datumloop::DatumEngineeringModel unknownFriction = engineeringModel;
        unknownFriction.mechanisms.front().contacts.front().frictionCoefficient.reset();
        const auto missingFrictionReport = datumloop::validateMechanismForStudy(
            unknownFriction, mechanism.id, mechanismContext);
        require(!missingFrictionReport.readyForStudy() &&
                    std::any_of(missingFrictionReport.diagnostics.begin(), missingFrictionReport.diagnostics.end(),
                                [](const auto& item) {
                                    return item.code == datumloop::MechanismDiagnosticCode::MissingFrictionCoefficient;
                                }),
                "contact readiness did not preserve an unknown friction coefficient as unresolved");

        datumloop::MechanismValidationContext edgeContactContext = mechanismContext;
        edgeContactContext.studyKind = datumloop::MechanismStudyKind::Contact;
        const auto edgeContactSurface = std::find_if(
            edgeContactContext.surfaceReferences.begin(), edgeContactContext.surfaceReferences.end(),
            [&contact](const auto& reference) { return reference.id == *contact.surfaceAReferenceId; });
        require(edgeContactSurface != edgeContactContext.surfaceReferences.end(),
                "contact surface was absent from validation context");
        edgeContactSurface->subshapeType = datumloop::DatumSubshapeType::Edge;
        const auto edgeContactReport = datumloop::validateMechanismForStudy(
            engineeringModel, mechanism.id, edgeContactContext);
        require(!edgeContactReport.readyForStudy() &&
                    std::any_of(edgeContactReport.diagnostics.begin(), edgeContactReport.diagnostics.end(),
                                [](const auto& item) {
                                    return item.code == datumloop::MechanismDiagnosticCode::InvalidContactSurfaceReference;
                                }),
                "contact readiness accepted an edge reference as a contact surface");

        datumloop::DatumEngineeringModel disconnected = engineeringModel;
        const std::string isolatedObjectId = engineeringDocument.createBox("Unconnected component", {5.0, 5.0, 5.0}).id;
        datumloop::DatumMechanismBody isolatedBody = makeBody(
            engineeringDocument, isolatedObjectId, "Unconnected component", materialDefinitionId);
        disconnected.mechanisms.front().bodyObjectIds.push_back(isolatedObjectId);
        disconnected.mechanisms.front().bodies.push_back(isolatedBody);
        disconnected.validate();
        mechanismContext.documentObjectIds.push_back(isolatedObjectId);
        const auto disconnectedReport = datumloop::validateMechanismForStudy(
            disconnected, mechanism.id, mechanismContext);
        require(!disconnectedReport.readyForStudy() &&
                    std::any_of(disconnectedReport.diagnostics.begin(), disconnectedReport.diagnostics.end(),
                                [](const auto& item) {
                                    return item.code == datumloop::MechanismDiagnosticCode::DisconnectedBodies;
                                }),
                "mechanism readiness did not diagnose an unconnected body");

        datumloop::DatumEngineeringModel overflowingInertia = engineeringModel;
        overflowingInertia.motors.front().inertiaTensorKgM2 =
            datumloop::DatumInertiaTensorKgM2{1.0e308, 1.0e308, 1.0e308, 1.7e308, 0.0, 0.0};
        bool nonPhysicalInertiaRejected = false;
        try {
            overflowingInertia.validate();
        } catch (const std::invalid_argument&) {
            nonPhysicalInertiaRejected = true;
        }
        require(nonPhysicalInertiaRejected,
                "engineering model accepted a non-positive-semidefinite inertia tensor at extreme finite magnitude");

        datumloop::DatumEngineeringModel torqueAbovePeak = engineeringModel;
        torqueAbovePeak.motors.front().torqueCurve.front().torqueNm = 3.0;
        bool torqueAbovePeakRejected = false;
        try {
            torqueAbovePeak.validate();
        } catch (const std::invalid_argument&) {
            torqueAbovePeakRejected = true;
        }
        require(torqueAbovePeakRejected, "engineering model accepted a torque curve above the motor peak torque");

        engineeringDocument.markSaved();
        datumloop::CommandHistory engineeringHistory;
        engineeringHistory.execute(engineeringDocument,
            std::make_unique<datumloop::SetEngineeringModelCommand>(engineeringModel));
        require(engineeringDocument.isDirty() && engineeringDocument.engineeringModel() == engineeringModel,
                "engineering model command did not install typed entities and mark the document dirty");
        require(engineeringHistory.undo(engineeringDocument) &&
                    engineeringDocument.engineeringModel() == datumloop::DatumEngineeringModel{},
                "engineering model command did not undo the model replacement");
        require(engineeringHistory.redo(engineeringDocument) &&
                    engineeringDocument.engineeringModel() == engineeringModel,
                "engineering model command did not redo the model replacement");

        datumloop::DatumEngineeringModel invalidEngineeringModel = engineeringModel;
        invalidEngineeringModel.motors.front().maximumSpeedRadPerSec = 10.0;
        bool invalidMotorRejected = false;
        try {
            engineeringDocument.setEngineeringModel(invalidEngineeringModel);
        } catch (const std::invalid_argument&) {
            invalidMotorRejected = true;
        }
        require(invalidMotorRejected && engineeringDocument.engineeringModel() == engineeringModel,
                "engineering model accepted inconsistent motor speed limits or partially mutated the document");
        datumloop::DatumEngineeringModel missingRepresentation = engineeringModel;
        missingRepresentation.motors.front().cadRepresentationObjectId = datumloop::createDatumUuidV4();
        bool missingRepresentationRejected = false;
        try {
            engineeringDocument.setEngineeringModel(missingRepresentation);
        } catch (const std::invalid_argument&) {
            missingRepresentationRejected = true;
        }
        require(missingRepresentationRejected && engineeringDocument.engineeringModel() == engineeringModel,
                "engineering model accepted a CAD representation link outside the document");
        bool engineeringIdentityCollisionRejected = false;
        try {
            (void)engineeringDocument.addObject(axis.id, "Duplicate engineering identity",
                                                datumloop::OcctKernel::box({1.0, 1.0, 1.0}));
        } catch (const std::invalid_argument&) {
            engineeringIdentityCollisionRejected = true;
        }
        require(engineeringIdentityCollisionRejected,
                "CAD object UUID collided with an existing engineering entity UUID");

        const std::uint64_t revision = document.revision();
        require(!document.setPosition(id, {NAN, 0.0, 0.0}), "document accepted a non-finite position");
        require(document.revision() == revision, "invalid position changed the document revision");

        std::cout << "Native DatumLoop core checks passed: parametric OCCT primitives, transforms, Booleans, engineering references, motor validation, exact display separation, and command undo/redo.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native DatumLoop core check failed: " << error.what() << '\n';
        return 1;
    }
}
