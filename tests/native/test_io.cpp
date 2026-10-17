#include "datumloop/core/DatumDocument.hpp"
#include "datumloop/geometry/OcctKernel.hpp"
#include "datumloop/io/DldProjectStore.hpp"
#include "datumloop/topology/DatumTopologyResolver.hpp"

#include <TopTools_FormatVersion.hxx>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

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

QString uuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void writeObject(const QString& path, const QJsonObject& object)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "could not write test project JSON");
    require(file.write(QJsonDocument(object).toJson()) > 0, "test project JSON write failed");
}

QJsonObject removeVersionSixMechanismFields(QJsonObject root)
{
    QJsonObject engineering = root.value(QStringLiteral("engineeringModel")).toObject();
    QJsonArray mechanisms = engineering.value(QStringLiteral("mechanisms")).toArray();
    for (qsizetype index = 0; index < mechanisms.size(); ++index) {
        QJsonObject mechanism = mechanisms.at(index).toObject();
        for (const QString& key : {QStringLiteral("specVersion"), QStringLiteral("bodies"),
                                   QStringLiteral("joints"), QStringLiteral("actuators"),
                                   QStringLiteral("contacts"), QStringLiteral("requirements")}) {
            mechanism.remove(key);
        }
        mechanisms.replace(index, mechanism);
    }
    engineering.insert(QStringLiteral("mechanisms"), mechanisms);
    root.insert(QStringLiteral("engineeringModel"), engineering);
    return root;
}

bool loadFails(const QString& path)
{
    try {
        (void)datumloop::DldProjectStore::load(path);
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    try {
        QTemporaryDir temporaryDirectory;
        require(temporaryDirectory.isValid(), "could not create temporary directory");
        const QString projectPath = temporaryDirectory.filePath(QStringLiteral("assembly.dld"));

        datumloop::DatumDocument document("Round-trip assembly");
        const std::string projectId = document.projectId();
        const std::string parentId = document.createBox("Base", {40.0, 30.0, 20.0}).id;
        document.setProperty(parentId, "reference", true);
        document.setProperty(parentId, "serial_number", std::int64_t{9007199254740993LL});
        document.setProperty(parentId, "roughness_um", 1.6);
        document.setProperty(parentId, "material", std::string("6061-T6"));
        const std::string childId = uuid().toStdString();
        (void)document.addObject(childId, "Carriage", datumloop::OcctKernel::box({12.0, 8.0, 4.0}),
                                 {12.5, -3.25, 9.0}, parentId);
        std::vector<datumloop::DatumTopologyReference> capturedReferences;
        std::copy_if(document.topologyReferences().begin(), document.topologyReferences().end(),
                     std::back_inserter(capturedReferences), [&parentId](const auto& reference) {
                         return reference.objectId == parentId;
                     });
        require(capturedReferences.size() == 26,
                "box topology capture should include its faces, edges, and vertices");
        datumloop::DatumTopologyReference persistedFeatureReference = capturedReferences.front();
        persistedFeatureReference.id = uuid().toStdString();
        persistedFeatureReference.featureId = uuid().toStdString();
        (void)document.addTopologyReference(persistedFeatureReference);

        datumloop::DatumEngineeringModel engineeringModel;
        datumloop::DatumMechanism mechanism;
        mechanism.id = uuid().toStdString();
        mechanism.name = "Round-trip mechanism";
        mechanism.bodyObjectIds = {parentId, childId};
        const auto parentObject = document.object(parentId);
        require(parentObject != nullptr && parentObject->featureGraph.outputFeatureId().has_value(),
                "round-trip parent has no native feature source");
        datumloop::DatumMechanismBody baseBody;
        baseBody.id = uuid().toStdString();
        baseBody.name = "Base body";
        baseBody.cadObjectId = parentId;
        baseBody.geometrySource = {datumloop::DatumGeometrySourceKind::NativeFeatureGraph,
                                   *parentObject->featureGraph.outputFeatureId(), "feature-graph-v1"};
        baseBody.massKg = 3.0;
        baseBody.centerOfMassLocalM = datumloop::Vector3{0.01, 0.0, 0.005};
        baseBody.inertiaTensorKgM2 = datumloop::DatumInertiaTensorKgM2{0.02, 0.03, 0.04, 0.0, 0.0, 0.0};
        baseBody.propertyEvidence = {
            {"massKg", datumloop::DatumPropertyEvidenceKind::Measured, "fixture scale", "scale-run-1", 0.99},
            {"centerOfMassLocalM", datumloop::DatumPropertyEvidenceKind::GeometryDerived, "CAD mass properties", "feature-graph-v1", 1.0},
            {"inertiaTensorKgM2", datumloop::DatumPropertyEvidenceKind::GeometryDerived, "CAD mass properties", "feature-graph-v1", 1.0},
        };
        datumloop::DatumMechanismBody movingBody;
        movingBody.id = uuid().toStdString();
        movingBody.name = "Moving carriage";
        movingBody.cadObjectId = childId;
        movingBody.geometrySource = {datumloop::DatumGeometrySourceKind::EmbeddedBRep, childId, "import-record-1"};
        movingBody.massKg = 0.8;
        movingBody.centerOfMassLocalM = datumloop::Vector3{0.0, 0.0, 0.0};
        movingBody.inertiaTensorKgM2 = datumloop::DatumInertiaTensorKgM2{0.0005, 0.0008, 0.001, 0.0, 0.0, 0.0};
        movingBody.propertyEvidence = {
            {"massKg", datumloop::DatumPropertyEvidenceKind::UserSpecified, "test fixture", "fixture-v1", 1.0},
            {"centerOfMassLocalM", datumloop::DatumPropertyEvidenceKind::Estimated, "test fixture estimate", "fixture-v1", 0.7},
            {"inertiaTensorKgM2", datumloop::DatumPropertyEvidenceKind::Estimated, "test fixture estimate", "fixture-v1", 0.7},
        };
        mechanism.bodies = {baseBody, movingBody};
        datumloop::DatumMotor motor;
        motor.id = uuid().toStdString();
        motor.mechanismId = mechanism.id;
        motor.name = "Round-trip servo";
        motor.cadRepresentationObjectId = parentId;
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
        motor.mountingReferences = {{"housing flange", parentId, capturedReferences.front().id}};
        motor.supplierData = datumloop::DatumMotorSupplierData{
            std::string("Datum Components"), std::string("DL-SV-48-100"),
            std::string("https://example.invalid/dl-sv-48-100")};
        datumloop::DatumAxis axis;
        axis.id = uuid().toStdString();
        axis.name = "X travel";
        axis.type = datumloop::DatumAxisType::Linear;
        axis.baseObjectId = parentId;
        axis.movingObjectId = childId;
        axis.directionInBaseFrame = {1.0, 0.0, 0.0};
        axis.actuatorMotorId = motor.id;
        mechanism.axes.push_back(axis);
        datumloop::DatumJoint slideJoint;
        slideJoint.id = uuid().toStdString();
        slideJoint.name = "Carriage slide";
        slideJoint.type = datumloop::DatumJointType::Prismatic;
        slideJoint.bodyAId = baseBody.id;
        slideJoint.bodyBId = movingBody.id;
        slideJoint.frameInBodyA.originM = {0.0, 0.0, 0.02};
        slideJoint.frameInBodyB.originM = {0.0, 0.0, -0.01};
        slideJoint.lowerLimitSi = 0.0;
        slideJoint.upperLimitSi = 0.15;
        mechanism.joints.push_back(slideJoint);
        datumloop::DatumActuator actuator;
        actuator.id = uuid().toStdString();
        actuator.name = "Servo slide command";
        actuator.jointId = slideJoint.id;
        actuator.mode = datumloop::DatumActuatorControlMode::Position;
        actuator.motorId = motor.id;
        actuator.trajectory = {{0.0, 0.0}, {0.5, 0.075}, {1.0, 0.15}};
        actuator.maximumEffortSi = 120.0;
        actuator.maximumRateSi = 0.1;
        mechanism.actuators.push_back(actuator);
        const auto parentFace = std::find_if(capturedReferences.begin(), capturedReferences.end(), [](const auto& reference) {
            return reference.subshapeType == datumloop::DatumSubshapeType::Face;
        });
        const auto childFace = std::find_if(document.topologyReferences().begin(), document.topologyReferences().end(),
                                             [&childId](const auto& reference) {
                                                 return reference.objectId == childId &&
                                                        reference.subshapeType == datumloop::DatumSubshapeType::Face;
                                             });
        require(parentFace != capturedReferences.end() && childFace != document.topologyReferences().end(),
                "round-trip mechanism fixture lacks explicit contact surface references");
        datumloop::DatumContactPair contact;
        contact.id = uuid().toStdString();
        contact.name = "Carriage guide contact";
        contact.bodyAId = baseBody.id;
        contact.bodyBId = movingBody.id;
        contact.surfaceAReferenceId = parentFace->id;
        contact.surfaceBReferenceId = childFace->id;
        contact.law = datumloop::DatumContactLaw::Coulomb;
        contact.frictionCoefficient = 0.12;
        contact.normalStiffnessNPerM = 1.0e7;
        contact.normalDampingNsPerM = 100.0;
        contact.restitution = 0.05;
        mechanism.contacts.push_back(contact);
        datumloop::DatumRequirement travel;
        travel.id = uuid().toStdString();
        travel.name = "Minimum axis travel";
        travel.metricId = "axis.travel";
        travel.unit = "m";
        travel.relation = datumloop::DatumRequirementRelation::GreaterOrEqual;
        travel.minimumSi = 0.12;
        travel.hardConstraint = true;
        mechanism.requirements.push_back(travel);
        engineeringModel.mechanisms.push_back(mechanism);
        engineeringModel.motors.push_back(motor);
        datumloop::DatumMotor unspecifiedMotor;
        unspecifiedMotor.id = uuid().toStdString();
        unspecifiedMotor.mechanismId = mechanism.id;
        unspecifiedMotor.name = "Unspecified motor";
        engineeringModel.motors.push_back(unspecifiedMotor);
        datumloop::DatumElectronicsModel electronicsModel;
        datumloop::MaterialDefinition fr4;
        fr4.id = uuid().toStdString();
        fr4.name = "FR-4 (composition and properties unspecified)";
        fr4.provenance = {"Named from imported board metadata; no physical values inferred."};
        datumloop::MaterialDefinition copper;
        copper.id = uuid().toStdString();
        copper.name = "Copper (grade unspecified)";
        copper.provenance = {"Material label only; conductivity and temper are unknown."};
        electronicsModel.materials = {fr4, copper};

        datumloop::CircuitDocument circuit;
        circuit.id = uuid().toStdString();
        circuit.name = "Board netlist";
        datumloop::CircuitComponent resistor;
        resistor.id = uuid().toStdString();
        resistor.sourceId = "cmp-r1";
        resistor.reference = "R1";
        resistor.displayName = "Resistor";
        resistor.kind = datumloop::CircuitComponentKind::Resistor;
        resistor.electricalValue = 1000.0;
        resistor.electricalValueUnit = "ohm";
        datumloop::CircuitPin pinA{uuid().toStdString(), resistor.id, "1", "A"};
        datumloop::CircuitPin pinB{uuid().toStdString(), resistor.id, "2", "B"};
        resistor.pinIds = {pinA.id, pinB.id};
        datumloop::CircuitNet signalNet{uuid().toStdString(), "net-signal", "SIG", false, {pinA.id}};
        datumloop::CircuitNet groundNet{uuid().toStdString(), "net-ground", "GND", true, {pinB.id}};
        circuit.components.push_back(resistor);
        circuit.pins = {pinA, pinB};
        circuit.nets = {signalNet, groundNet};
        electronicsModel.circuits.push_back(circuit);

        datumloop::PcbDocument pcb;
        pcb.id = uuid().toStdString();
        pcb.name = "Reference PCB";
        pcb.circuitDocumentId = circuit.id;
        pcb.boardMaterialDefinitionId = fr4.id;
        pcb.widthM = 0.04;
        pcb.heightM = 0.03;
        pcb.thicknessM = 0.0016;
        pcb.stackup.id = uuid().toStdString();
        const std::string frontLayerId = uuid().toStdString();
        const std::string backLayerId = uuid().toStdString();
        pcb.stackup.layers = {
            {frontLayerId, "F.Cu", datumloop::PcbLayerRole::Copper, copper.id, 35e-6, {}, {}, {}},
            {backLayerId, "B.Cu", datumloop::PcbLayerRole::Copper, copper.id, 35e-6, {}, {}, {}},
        };
        pcb.components.push_back({uuid().toStdString(), "pcb-cmp-r1", "R1", "R_0805",
                                  resistor.id, {0.01, 0.015}, 0.0, 0.002, 0.00125, {}, "F.Cu"});
        pcb.pads.push_back({uuid().toStdString(), "pad-1", pcb.components.front().id, pinA.id,
                            signalNet.id, datumloop::PcbPadShape::Rectangle, {0.009, 0.015},
                            0.001, 0.001, 0.0, {frontLayerId}});
        pcb.traces.push_back({uuid().toStdString(), "trace-1", signalNet.id, frontLayerId,
                              0.00025, {{0.009, 0.015}, {0.02, 0.015}}});
        pcb.vias.push_back({uuid().toStdString(), "via-1", groundNet.id, {0.03, 0.02},
                            0.0007, 0.0003, {frontLayerId, backLayerId}});
        electronicsModel.pcbs.push_back(pcb);
        document.setElectronicsModel(electronicsModel);
        document.setEngineeringModel(engineeringModel);

        const TopoDS_Shape regeneratedBox = datumloop::OcctKernel::box({40.0, 30.0, 20.0});
        for (const datumloop::DatumTopologyReference& reference : capturedReferences) {
            const auto resolution = datumloop::DatumTopologyResolver::resolve(
                reference, parentId, regeneratedBox, {}, document.object(parentId)->featureGraph.outputFeatureId());
            require(resolution.status == datumloop::DatumTopologyResolutionStatus::Resolved &&
                        !resolution.subshape.IsNull(),
                    "deterministic topology matching failed on identical regenerated box geometry");
            const std::optional<datumloop::Vector3> hitNormal =
                reference.subshapeType == datumloop::DatumSubshapeType::Face
                    ? reference.geometrySignature.direction
                    : std::nullopt;
            const auto hit = datumloop::DatumTopologyResolver::resolveHitAtPoint(
                capturedReferences, parentId, regeneratedBox, reference.subshapeType,
                reference.geometrySignature.centroidMm, hitNormal, 0.5, {},
                document.object(parentId)->featureGraph.outputFeatureId());
            require(hit.status == datumloop::DatumTopologyResolutionStatus::Resolved &&
                        hit.referenceId == reference.id,
                    "exact point picking did not resolve to the persistent face, edge, or vertex identity");
        }
        auto ownerMismatch = datumloop::DatumTopologyResolver::resolve(
            capturedReferences.front(), childId, regeneratedBox);
        require(ownerMismatch.status == datumloop::DatumTopologyResolutionStatus::OwnerMismatch,
                "topology resolver accepted a reference from a different object");
        datumloop::DatumTopologyReference featureReference = capturedReferences.front();
        featureReference.featureId = uuid().toStdString();
        require(datumloop::DatumTopologyResolver::resolve(featureReference, parentId, regeneratedBox).status ==
                    datumloop::DatumTopologyResolutionStatus::UnsupportedFeature,
                "topology resolver pretended to resolve unsupported feature provenance");

        const std::string cubeId = uuid().toStdString();
        auto cubeReferences = datumloop::DatumTopologyResolver::captureAll(
            cubeId, datumloop::OcctKernel::box({10.0, 10.0, 10.0}));
        datumloop::DatumTopologyReference deliberatelyAmbiguous = cubeReferences.front();
        deliberatelyAmbiguous.geometrySignature.centroidMm = {0.0, 0.0, 0.0};
        deliberatelyAmbiguous.geometrySignature.direction.reset();
        datumloop::DatumTopologyResolveOptions permissiveAmbiguityCheck;
        permissiveAmbiguityCheck.maximumCentroidDistanceRatio = 1.0;
        permissiveAmbiguityCheck.maximumScore = 1.0;
        deliberatelyAmbiguous.transientIndexHint = 0;
        require(datumloop::DatumTopologyResolver::resolve(
                    deliberatelyAmbiguous, cubeId,
                    datumloop::OcctKernel::box({10.0, 10.0, 10.0}), permissiveAmbiguityCheck).status ==
                    datumloop::DatumTopologyResolutionStatus::Ambiguous,
                "topology resolver guessed a face when geometric signatures tied");
        auto invalidResolveOptions = permissiveAmbiguityCheck;
        invalidResolveOptions.maximumScore = std::numeric_limits<double>::quiet_NaN();
        bool invalidResolveOptionsRejected = false;
        try {
            (void)datumloop::DatumTopologyResolver::resolve(
                deliberatelyAmbiguous, cubeId, datumloop::OcctKernel::box({10.0, 10.0, 10.0}),
                invalidResolveOptions);
        } catch (const std::invalid_argument&) {
            invalidResolveOptionsRejected = true;
        }
        require(invalidResolveOptionsRejected, "topology resolver accepted non-finite matching tolerances");

        require(document.isDirty(), "new or edited document should be dirty before saving");
        datumloop::DldProjectStore::save(document, projectPath);
        require(!document.isDirty(), "successful save should mark document clean");

        QFile manifest(projectPath);
        require(manifest.open(QIODevice::ReadOnly), "saved project could not be opened");
        const QByteArray manifestBytes = manifest.readAll();
        const QJsonObject root = QJsonDocument::fromJson(manifestBytes).object();
        require(root.value(QStringLiteral("schemaVersion")).toInt() == datumloop::DldProjectStore::SchemaVersion,
                "saved project did not include the current schema");
        require(root.value(QStringLiteral("engineeringModel")).toObject()
                    .value(QStringLiteral("mechanisms")).toArray().size() == 1 &&
                    root.value(QStringLiteral("engineeringModel")).toObject()
                    .value(QStringLiteral("motors")).toArray().size() == 2,
                "saved project omitted engineering mechanisms or motors");
        const QJsonObject encodedMechanism = root.value(QStringLiteral("engineeringModel")).toObject()
            .value(QStringLiteral("mechanisms")).toArray().at(0).toObject();
        require(encodedMechanism.value(QStringLiteral("specVersion")).toInt() == 1 &&
                    encodedMechanism.value(QStringLiteral("bodies")).toArray().size() == 2 &&
                    encodedMechanism.value(QStringLiteral("joints")).toArray().size() == 1 &&
                    encodedMechanism.value(QStringLiteral("actuators")).toArray().size() == 1 &&
                    encodedMechanism.value(QStringLiteral("contacts")).toArray().size() == 1 &&
                    encodedMechanism.value(QStringLiteral("requirements")).toArray().size() == 1,
                "schema-six project omitted versioned bodies, joints, actuators, contacts, or requirements");
        require(root.value(QStringLiteral("topologyReferences")).toArray().size() ==
                    static_cast<qsizetype>(document.topologyReferences().size()),
                "saved project omitted its persistent topology references");
        const QJsonArray savedObjects = root.value(QStringLiteral("objects")).toArray();
        require(savedObjects.at(0).toObject().value(QStringLiteral("featureDefinitions")).toArray().size() == 1 &&
                    savedObjects.at(0).toObject().value(QStringLiteral("outputFeatureId")).toString() ==
                        QString::fromStdString(document.object(parentId)->featureGraph.outputFeatureId().value()) &&
                    savedObjects.at(1).toObject().value(QStringLiteral("featureDefinitions")).toArray().isEmpty() &&
                    savedObjects.at(0).toObject().value(QStringLiteral("references")).toArray().isEmpty(),
                "feature graph or reserved non-topological reference fields were not encoded correctly");
        require(savedObjects.at(0).toObject().value(QStringLiteral("geometry")).toObject()
                    .value(QStringLiteral("occtFormatVersion")).toInt() == TopTools_FormatVersion_CURRENT,
                "saved B-Rep omitted its OCCT stream version");
        require(!manifestBytes.contains("displayMesh") && !manifestBytes.contains("triangles"),
                "project persistence should not store renderer tessellation");

        datumloop::DatumDocument loaded = datumloop::DldProjectStore::load(projectPath);
        require(!loaded.isDirty(), "loaded project should start clean");
        require(loaded.engineeringModel() == engineeringModel,
                "engineering entities, SI values, optional unknowns, or CAD references changed during round-trip");
        require(loaded.electronicsModel() == electronicsModel,
                "materials, circuits, stackup, pads, traces, or vias changed during round-trip");
        require(loaded.projectId() == projectId, "project UUID did not survive round-trip");
        require(loaded.name() == "Round-trip assembly", "project name did not survive round-trip");
        require(loaded.objects().size() == 2, "project object count did not survive round-trip");
        const datumloop::DatumObject* loadedParent = loaded.object(parentId);
        const datumloop::DatumObject* loadedChild = loaded.object(childId);
        require(loadedParent != nullptr && loadedChild != nullptr,
                "stable object UUIDs did not survive round-trip");
        require(loadedParent->featureGraph.features().size() == 1 &&
                    loadedParent->featureGraph.outputSucceeded() &&
                    loadedParent->featureGraph.outputFeatureId() ==
                        document.object(parentId)->featureGraph.outputFeatureId(),
                "parametric feature graph did not survive exact-geometry round-trip");
        require(loadedParent->featureGraph.features().front().status == datumloop::DatumFeatureStatus::Succeeded &&
                    !loadedParent->featureGraph.features().front().generatedShape.IsNull(),
                "loaded feature graph did not recompute an exact generated shape");
        require(!loadedParent->parentId.has_value(), "root object acquired a parent during round-trip");
        require(loadedChild->parentId == parentId, "object hierarchy did not survive round-trip");
        require(close(loadedChild->position[0], 12.5) && close(loadedChild->position[1], -3.25) &&
                    close(loadedChild->position[2], 9.0),
                "object translation did not survive round-trip");
        require(close(loadedChild->metrics.volume, 384.0) && loadedChild->metrics.valid,
                "exact OCCT B-Rep geometry or its recomputed metrics did not survive round-trip");
        require(datumloop::OcctKernel::tessellate(loadedChild->shape).triangleCount() > 0,
                "display tessellation could not be regenerated after load");
        require(std::get<bool>(loadedParent->properties.at("reference")),
                "boolean property did not survive round-trip");
        require(std::get<std::int64_t>(loadedParent->properties.at("serial_number")) == 9007199254740993LL,
                "64-bit integer property lost precision during round-trip");
        require(close(std::get<double>(loadedParent->properties.at("roughness_um")), 1.6),
                "floating-point property did not survive round-trip");
        require(std::get<std::string>(loadedParent->properties.at("material")) == "6061-T6",
                "string property did not survive round-trip");
        require(loaded.topologyReferences().size() == document.topologyReferences().size(),
                "persistent topology references did not survive project round-trip");
        for (const datumloop::DatumTopologyReference& reference : capturedReferences) {
            const auto loadedReference = std::find_if(
                loaded.topologyReferences().begin(), loaded.topologyReferences().end(),
                [&reference](const auto& candidate) { return candidate.id == reference.id; });
            require(loadedReference != loaded.topologyReferences().end(),
                    "persistent topology reference UUID did not survive round-trip");
            require(loadedReference->objectId == parentId &&
                        loadedReference->subshapeType == reference.subshapeType &&
                        loadedReference->topologySignature == reference.topologySignature &&
                        loadedReference->geometrySignature == reference.geometrySignature &&
                        loadedReference->parentReferenceIds == reference.parentReferenceIds,
                    "persistent topology signature or parent relationship changed during round-trip");
        }
        const auto loadedFeatureReference = std::find_if(
            loaded.topologyReferences().begin(), loaded.topologyReferences().end(),
            [&persistedFeatureReference](const auto& candidate) {
                return candidate.id == persistedFeatureReference.id;
            });
        require(loadedFeatureReference != loaded.topologyReferences().end() &&
                    loadedFeatureReference->featureId == persistedFeatureReference.featureId,
                "persistent topology feature provenance did not survive project round-trip");

        QJsonObject versionOne = root;
        versionOne.insert(QStringLiteral("schemaVersion"), 1);
        versionOne.remove(QStringLiteral("topologyReferences"));
        versionOne.remove(QStringLiteral("engineeringModel"));
        versionOne.remove(QStringLiteral("electronicsModel"));
        QJsonArray legacyObjects = versionOne.value(QStringLiteral("objects")).toArray();
        for (qsizetype index = 0; index < legacyObjects.size(); ++index) {
            QJsonObject legacyObject = legacyObjects.at(index).toObject();
            legacyObject.insert(QStringLiteral("featureDefinitions"), QJsonArray{});
            legacyObject.remove(QStringLiteral("outputFeatureId"));
            legacyObjects.replace(index, legacyObject);
        }
        versionOne.insert(QStringLiteral("objects"), legacyObjects);
        const QString versionOnePath = temporaryDirectory.filePath(QStringLiteral("schema-v1.dld"));
        writeObject(versionOnePath, versionOne);
        datumloop::DatumDocument migratedVersionOne = datumloop::DldProjectStore::load(versionOnePath);
        require(migratedVersionOne.topologyReferences().size() == 52 && !migratedVersionOne.isDirty(),
                "schema-v1 project did not receive initial persistent topology references");
        datumloop::DldProjectStore::save(migratedVersionOne, versionOnePath);
        QFile migratedManifest(versionOnePath);
        require(migratedManifest.open(QIODevice::ReadOnly), "migrated project could not be reopened");
        const QJsonObject migratedRoot = QJsonDocument::fromJson(migratedManifest.readAll()).object();
        require(migratedRoot.value(QStringLiteral("schemaVersion")).toInt() ==
                    datumloop::DldProjectStore::SchemaVersion &&
                    migratedRoot.value(QStringLiteral("topologyReferences")).toArray().size() == 52,
                "saving a schema-v1 project did not write the current schema");

        QJsonObject versionTwo = versionOne;
        versionTwo.insert(QStringLiteral("schemaVersion"), 2);
        versionTwo.insert(QStringLiteral("topologyReferences"), root.value(QStringLiteral("topologyReferences")));
        QJsonArray versionTwoReferences = versionTwo.value(QStringLiteral("topologyReferences")).toArray();
        for (qsizetype index = 0; index < versionTwoReferences.size(); ++index) {
            QJsonObject reference = versionTwoReferences.at(index).toObject();
            reference.insert(QStringLiteral("featureId"), QJsonValue(QJsonValue::Null));
            versionTwoReferences.replace(index, reference);
        }
        versionTwo.insert(QStringLiteral("topologyReferences"), versionTwoReferences);
        const QString versionTwoPath = temporaryDirectory.filePath(QStringLiteral("schema-v2.dld"));
        writeObject(versionTwoPath, versionTwo);
        datumloop::DatumDocument migratedVersionTwo = datumloop::DldProjectStore::load(versionTwoPath);
        require(migratedVersionTwo.topologyReferences().size() == 53 &&
                    migratedVersionTwo.object(parentId)->featureGraph.features().empty() &&
                    !migratedVersionTwo.isDirty(),
                "schema-v2 project did not migrate as exact-geometry-only document data");
        datumloop::DldProjectStore::save(migratedVersionTwo, versionTwoPath);
        QFile migratedV2Manifest(versionTwoPath);
        require(migratedV2Manifest.open(QIODevice::ReadOnly), "migrated schema-v2 project could not be reopened");
        const QJsonObject migratedV2Root = QJsonDocument::fromJson(migratedV2Manifest.readAll()).object();
        require(migratedV2Root.value(QStringLiteral("schemaVersion")).toInt() ==
                    datumloop::DldProjectStore::SchemaVersion,
                "saving a schema-v2 project did not write the current schema");

        QJsonObject versionThree = root;
        versionThree.insert(QStringLiteral("schemaVersion"), 3);
        versionThree.remove(QStringLiteral("engineeringModel"));
        versionThree.remove(QStringLiteral("electronicsModel"));
        const QString versionThreePath = temporaryDirectory.filePath(QStringLiteral("schema-v3.dld"));
        writeObject(versionThreePath, versionThree);
        datumloop::DatumDocument migratedVersionThree = datumloop::DldProjectStore::load(versionThreePath);
        require(migratedVersionThree.engineeringModel() == datumloop::DatumEngineeringModel{} &&
                    migratedVersionThree.electronicsModel() == datumloop::DatumElectronicsModel{} &&
                    migratedVersionThree.objects().size() == 2 && !migratedVersionThree.isDirty(),
                "schema-v3 project did not migrate with empty engineering and electronics models");
        datumloop::DldProjectStore::save(migratedVersionThree, versionThreePath);
        QFile migratedV3Manifest(versionThreePath);
        require(migratedV3Manifest.open(QIODevice::ReadOnly), "migrated schema-v3 project could not be reopened");
        const QJsonObject migratedV3Root = QJsonDocument::fromJson(migratedV3Manifest.readAll()).object();
        require(migratedV3Root.value(QStringLiteral("schemaVersion")).toInt() ==
                    datumloop::DldProjectStore::SchemaVersion &&
                    migratedV3Root.value(QStringLiteral("engineeringModel")).toObject()
                    .value(QStringLiteral("motors")).toArray().isEmpty(),
                "saving a schema-v3 project did not write the current schema and empty engineering model");

        datumloop::DatumEngineeringModel legacyEngineeringModel = engineeringModel;
        legacyEngineeringModel.mechanisms.front().bodies.clear();
        legacyEngineeringModel.mechanisms.front().joints.clear();
        legacyEngineeringModel.mechanisms.front().actuators.clear();
        legacyEngineeringModel.mechanisms.front().contacts.clear();
        legacyEngineeringModel.mechanisms.front().requirements.clear();
        QJsonObject versionFive = removeVersionSixMechanismFields(root);
        versionFive.insert(QStringLiteral("schemaVersion"), 5);
        const QString versionFivePath = temporaryDirectory.filePath(QStringLiteral("schema-v5.dld"));
        writeObject(versionFivePath, versionFive);
        datumloop::DatumDocument migratedVersionFive = datumloop::DldProjectStore::load(versionFivePath);
        require(migratedVersionFive.engineeringModel() == legacyEngineeringModel &&
                    migratedVersionFive.electronicsModel() == electronicsModel &&
                    !migratedVersionFive.isDirty(),
                "schema-v5 project did not preserve legacy engineering and electronics data");
        datumloop::DldProjectStore::save(migratedVersionFive, versionFivePath);
        QFile migratedV5Manifest(versionFivePath);
        require(migratedV5Manifest.open(QIODevice::ReadOnly), "migrated schema-v5 project could not be reopened");
        const QJsonObject migratedV5Root = QJsonDocument::fromJson(migratedV5Manifest.readAll()).object();
        require(migratedV5Root.value(QStringLiteral("schemaVersion")).toInt() ==
                    datumloop::DldProjectStore::SchemaVersion,
                "saving a schema-v5 project did not write the current schema");

        QJsonObject versionFour = removeVersionSixMechanismFields(root);
        versionFour.insert(QStringLiteral("schemaVersion"), 4);
        versionFour.remove(QStringLiteral("electronicsModel"));
        const QString versionFourPath = temporaryDirectory.filePath(QStringLiteral("schema-v4.dld"));
        writeObject(versionFourPath, versionFour);
        datumloop::DatumDocument migratedVersionFour = datumloop::DldProjectStore::load(versionFourPath);
        require(migratedVersionFour.engineeringModel() == legacyEngineeringModel &&
                    migratedVersionFour.electronicsModel() == datumloop::DatumElectronicsModel{},
                "schema-v4 project did not preserve engineering data and default electronics data");
        datumloop::DldProjectStore::save(migratedVersionFour, versionFourPath);
        QFile migratedV4Manifest(versionFourPath);
        require(migratedV4Manifest.open(QIODevice::ReadOnly), "migrated schema-v4 project could not be reopened");
        const QJsonObject migratedV4Root = QJsonDocument::fromJson(migratedV4Manifest.readAll()).object();
        require(migratedV4Root.value(QStringLiteral("schemaVersion")).toInt() ==
                    datumloop::DldProjectStore::SchemaVersion &&
                    migratedV4Root.value(QStringLiteral("electronicsModel")).toObject()
                        .value(QStringLiteral("materials")).toArray().isEmpty(),
                "saving a schema-v4 project did not write the current electronics-aware schema");

        QJsonObject invalidSchemaThree = versionThree;
        invalidSchemaThree.insert(QStringLiteral("engineeringModel"), root.value(QStringLiteral("engineeringModel")));
        const QString invalidSchemaThreePath =
            temporaryDirectory.filePath(QStringLiteral("schema-v3-with-engineering-data.dld"));
        writeObject(invalidSchemaThreePath, invalidSchemaThree);
        require(loadFails(invalidSchemaThreePath),
                "schema-v3 project silently accepted schema-v4 engineering data");

        QJsonObject invalidEngineering = root;
        QJsonObject invalidModel = invalidEngineering.value(QStringLiteral("engineeringModel")).toObject();
        QJsonArray invalidMotors = invalidModel.value(QStringLiteral("motors")).toArray();
        QJsonObject invalidMotor = invalidMotors.at(0).toObject();
        invalidMotor.insert(QStringLiteral("mechanismId"), uuid());
        invalidMotors.replace(0, invalidMotor);
        invalidModel.insert(QStringLiteral("motors"), invalidMotors);
        invalidEngineering.insert(QStringLiteral("engineeringModel"), invalidModel);
        const QString invalidEngineeringPath =
            temporaryDirectory.filePath(QStringLiteral("invalid-engineering-model.dld"));
        writeObject(invalidEngineeringPath, invalidEngineering);
        require(loadFails(invalidEngineeringPath),
                "schema-v5 load accepted a motor with a missing mechanism reference");

        QJsonObject missingNullableField = root;
        QJsonObject missingFieldModel = missingNullableField.value(QStringLiteral("engineeringModel")).toObject();
        QJsonArray missingFieldMotors = missingFieldModel.value(QStringLiteral("motors")).toArray();
        QJsonObject motorWithMount = missingFieldMotors.at(0).toObject();
        QJsonArray motorMounts = motorWithMount.value(QStringLiteral("mountingReferences")).toArray();
        QJsonObject incompleteMount = motorMounts.at(0).toObject();
        incompleteMount.remove(QStringLiteral("topologyReferenceId"));
        motorMounts.replace(0, incompleteMount);
        motorWithMount.insert(QStringLiteral("mountingReferences"), motorMounts);
        missingFieldMotors.replace(0, motorWithMount);
        missingFieldModel.insert(QStringLiteral("motors"), missingFieldMotors);
        missingNullableField.insert(QStringLiteral("engineeringModel"), missingFieldModel);
        const QString missingNullablePath =
            temporaryDirectory.filePath(QStringLiteral("missing-engineering-nullable-field.dld"));
        writeObject(missingNullablePath, missingNullableField);
        require(loadFails(missingNullablePath),
                "schema-v5 load accepted an omitted nullable engineering field");

        datumloop::DatumDocument booleanDocument("Boolean feature round-trip");
        const std::string booleanObjectId = booleanDocument.createBox("Boolean result", {8.0, 8.0, 8.0}).id;
        const std::string leftId = uuid().toStdString();
        const std::string rightId = uuid().toStdString();
        const std::string translatedId = uuid().toStdString();
        const std::string cutFeatureId = uuid().toStdString();
        datumloop::DatumFeature leftFeature;
        leftFeature.id = leftId;
        leftFeature.ownerObjectId = booleanObjectId;
        leftFeature.type = datumloop::DatumFeatureType::Box;
        leftFeature.parameters = {{"xMm", 10.0}, {"yMm", 10.0}, {"zMm", 10.0}};
        datumloop::DatumFeature rightFeature = leftFeature;
        rightFeature.id = rightId;
        datumloop::DatumFeature translatedFeature;
        translatedFeature.id = translatedId;
        translatedFeature.ownerObjectId = booleanObjectId;
        translatedFeature.type = datumloop::DatumFeatureType::Translate;
        translatedFeature.inputFeatureIds = {rightId};
        translatedFeature.parameters = {{"xMm", 5.0}, {"yMm", 0.0}, {"zMm", 0.0}};
        datumloop::DatumFeature cutFeature;
        cutFeature.id = cutFeatureId;
        cutFeature.ownerObjectId = booleanObjectId;
        cutFeature.type = datumloop::DatumFeatureType::BooleanCut;
        cutFeature.inputFeatureIds = {leftId, translatedId};
        datumloop::DatumFeatureGraph booleanGraph(
            booleanObjectId, {leftFeature, rightFeature, translatedFeature, cutFeature}, cutFeatureId);
        booleanDocument.setFeatureGraph(booleanObjectId, std::move(booleanGraph));
        const QString booleanProjectPath = temporaryDirectory.filePath(QStringLiteral("boolean-features.dld"));
        datumloop::DldProjectStore::save(booleanDocument, booleanProjectPath);
        const datumloop::DatumDocument loadedBoolean = datumloop::DldProjectStore::load(booleanProjectPath);
        const datumloop::DatumObject* loadedBooleanObject = loadedBoolean.object(booleanObjectId);
        require(loadedBooleanObject != nullptr && loadedBooleanObject->featureGraph.features().size() == 4 &&
                    loadedBooleanObject->featureGraph.outputFeatureId() == cutFeatureId &&
                    close(loadedBooleanObject->metrics.volume, 500.0) &&
                    std::all_of(loadedBooleanObject->featureGraph.features().begin(),
                                loadedBooleanObject->featureGraph.features().end(), [](const auto& feature) {
                                    return feature.status == datumloop::DatumFeatureStatus::Succeeded;
                                }),
                "Boolean feature DAG did not recompute and round-trip through schema 7");

        loaded.setName("Edited assembly");
        require(loaded.isDirty(), "document metadata edits should mark the document dirty");
        loaded.markSaved();
        require(!loaded.isDirty(), "markSaved should clear dirty state");
        require(loaded.setPosition(childId, {13.0, -3.25, 9.0}) && loaded.isDirty(),
                "transform edits should mark the document dirty");

        QJsonObject broken = root;
        broken.insert(QStringLiteral("schemaVersion"), 99);
        const QString unsupportedPath = temporaryDirectory.filePath(QStringLiteral("unsupported.dld"));
        writeObject(unsupportedPath, broken);
        require(loadFails(unsupportedPath), "unsupported project schema was accepted");

        QJsonArray objects = root.value(QStringLiteral("objects")).toArray();
        QJsonObject firstObject = objects.at(0).toObject();
        QJsonObject geometry = firstObject.value(QStringLiteral("geometry")).toObject();
        const QString unsupportedBrepPath = temporaryDirectory.filePath(QStringLiteral("unsupported-brep.dld"));
        broken = root;
        objects = broken.value(QStringLiteral("objects")).toArray();
        firstObject = objects.at(0).toObject();
        geometry = firstObject.value(QStringLiteral("geometry")).toObject();
        geometry.insert(QStringLiteral("occtFormatVersion"), 99);
        firstObject.insert(QStringLiteral("geometry"), geometry);
        objects.replace(0, firstObject);
        broken.insert(QStringLiteral("objects"), objects);
        writeObject(unsupportedBrepPath, broken);
        require(loadFails(unsupportedBrepPath), "unsupported OCCT B-Rep format version was accepted");

        const QString unsupportedFeaturePath = temporaryDirectory.filePath(QStringLiteral("unsupported-feature.dld"));
        broken = root;
        objects = broken.value(QStringLiteral("objects")).toArray();
        firstObject = objects.at(0).toObject();
        firstObject.insert(QStringLiteral("featureDefinitions"), QJsonArray{QJsonObject{
            {QStringLiteral("type"), QStringLiteral("primitive.box")},
        }});
        objects.replace(0, firstObject);
        broken.insert(QStringLiteral("objects"), objects);
        writeObject(unsupportedFeaturePath, broken);
        require(loadFails(unsupportedFeaturePath), "unsupported feature definitions were silently discarded");

        const QString unsupportedReferencePath = temporaryDirectory.filePath(QStringLiteral("unsupported-reference.dld"));
        broken = root;
        objects = broken.value(QStringLiteral("objects")).toArray();
        firstObject = objects.at(0).toObject();
        firstObject.insert(QStringLiteral("references"), QJsonArray{QStringLiteral("Face1")});
        objects.replace(0, firstObject);
        broken.insert(QStringLiteral("objects"), objects);
        writeObject(unsupportedReferencePath, broken);
        require(loadFails(unsupportedReferencePath), "unsupported topology references were silently discarded");

        const QString badBase64Path = temporaryDirectory.filePath(QStringLiteral("bad-base64.dld"));
        broken = root;
        objects = broken.value(QStringLiteral("objects")).toArray();
        firstObject = objects.at(0).toObject();
        geometry = firstObject.value(QStringLiteral("geometry")).toObject();
        geometry.insert(QStringLiteral("data"), QStringLiteral("not valid base64!"));
        firstObject.insert(QStringLiteral("geometry"), geometry);
        objects.replace(0, firstObject);
        broken.insert(QStringLiteral("objects"), objects);
        writeObject(badBase64Path, broken);
        require(loadFails(badBase64Path), "malformed base64 B-Rep data was accepted");

        const QString duplicateIdPath = temporaryDirectory.filePath(QStringLiteral("duplicate-id.dld"));
        broken = root;
        objects = broken.value(QStringLiteral("objects")).toArray();
        firstObject = objects.at(0).toObject();
        QJsonObject secondObject = objects.at(1).toObject();
        secondObject.insert(QStringLiteral("id"), firstObject.value(QStringLiteral("id")));
        objects.replace(1, secondObject);
        broken.insert(QStringLiteral("objects"), objects);
        writeObject(duplicateIdPath, broken);
        require(loadFails(duplicateIdPath), "duplicate object UUID was accepted");

        const QString projectIdCollisionPath = temporaryDirectory.filePath(QStringLiteral("project-id-collision.dld"));
        broken = root;
        objects = broken.value(QStringLiteral("objects")).toArray();
        firstObject = objects.at(0).toObject();
        firstObject.insert(QStringLiteral("id"), broken.value(QStringLiteral("project")).toObject()
                                                     .value(QStringLiteral("id")));
        objects.replace(0, firstObject);
        broken.insert(QStringLiteral("objects"), objects);
        writeObject(projectIdCollisionPath, broken);
        require(loadFails(projectIdCollisionPath), "object UUID collision with the project UUID was accepted");

        const QString missingParentPath = temporaryDirectory.filePath(QStringLiteral("missing-parent.dld"));
        broken = root;
        objects = broken.value(QStringLiteral("objects")).toArray();
        secondObject = objects.at(1).toObject();
        secondObject.insert(QStringLiteral("parentId"), uuid());
        objects.replace(1, secondObject);
        broken.insert(QStringLiteral("objects"), objects);
        writeObject(missingParentPath, broken);
        require(loadFails(missingParentPath), "object with a missing parent was accepted");

        const QString missingTopologyParentPath = temporaryDirectory.filePath(QStringLiteral("missing-topology-parent.dld"));
        broken = root;
        QJsonArray topologyReferences = broken.value(QStringLiteral("topologyReferences")).toArray();
        QJsonObject edgeReference = topologyReferences.at(6).toObject();
        edgeReference.insert(QStringLiteral("parentReferenceIds"), QJsonArray{uuid()});
        topologyReferences.replace(6, edgeReference);
        broken.insert(QStringLiteral("topologyReferences"), topologyReferences);
        writeObject(missingTopologyParentPath, broken);
        require(loadFails(missingTopologyParentPath), "topology reference with a missing parent was accepted");

        loaded.setName("Save failure should stay dirty");
        bool failedSaveWasRejected = false;
        try {
            datumloop::DldProjectStore::save(loaded,
                temporaryDirectory.filePath(QStringLiteral("missing-folder/failure.dld")));
        } catch (const std::exception&) {
            failedSaveWasRejected = true;
        }
        require(failedSaveWasRejected && loaded.isDirty(),
                "failed save should be rejected without clearing document dirty state");

        std::cout << "Native .dld and topology checks passed: exact B-Rep round-trip, typed properties, hierarchy, schema-v1 migration, persistent topology matching, ambiguity rejection, and malformed-file rejection.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native .dld check failed: " << error.what() << '\n';
        return 1;
    }
}
