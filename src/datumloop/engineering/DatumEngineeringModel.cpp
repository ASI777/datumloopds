#include "datumloop/engineering/DatumEngineeringModel.hpp"

#include "datumloop/core/DatumUuid.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace datumloop {
namespace {

bool hasText(const std::string& value)
{
    return value.find_first_not_of(" \t\n\r") != std::string::npos;
}

bool finite(const Vector3& value)
{
    return std::all_of(value.begin(), value.end(), [](double component) {
        return std::isfinite(component);
    });
}

void validateOptionalPositive(const std::optional<double>& value, const char* field)
{
    if (value.has_value() && (!std::isfinite(*value) || *value <= 0.0)) {
        throw std::invalid_argument(std::string(field) + " must be finite and positive when specified");
    }
}

void validateOptionalText(const std::optional<std::string>& value, const char* field)
{
    if (value.has_value() && !hasText(*value)) {
        throw std::invalid_argument(std::string(field) + " must not be empty when specified");
    }
}

void validateInertiaTensor(const DatumInertiaTensorKgM2& tensor)
{
    if (!std::all_of(tensor.begin(), tensor.end(), [](double component) {
            return std::isfinite(component);
        })) {
        throw std::invalid_argument("inertia tensor must contain finite kg m^2 values");
    }

    const double scale = std::max({std::abs(tensor[0]), std::abs(tensor[1]), std::abs(tensor[2]),
                                   std::abs(tensor[3]), std::abs(tensor[4]), std::abs(tensor[5])});
    if (scale == 0.0) {
        return;
    }
    // Normalize before evaluating principal minors so very large but finite
    // tensor values cannot overflow and bypass the positive-semidefinite test.
    const double ixx = tensor[0] / scale;
    const double iyy = tensor[1] / scale;
    const double izz = tensor[2] / scale;
    const double ixy = tensor[3] / scale;
    const double ixz = tensor[4] / scale;
    const double iyz = tensor[5] / scale;
    constexpr double tolerance = 1e-12;
    const double determinant = ixx * iyy * izz + 2.0 * ixy * ixz * iyz -
                               ixx * iyz * iyz - iyy * ixz * ixz - izz * ixy * ixy;
    if (ixx < -tolerance || iyy < -tolerance || izz < -tolerance ||
        ixx * iyy - ixy * ixy < -tolerance ||
        ixx * izz - ixz * ixz < -tolerance ||
        iyy * izz - iyz * iyz < -tolerance || determinant < -tolerance) {
        throw std::invalid_argument("inertia tensor must be positive semidefinite");
    }
}

void validateMotor(const DatumMotor& motor)
{
    if (!isDatumUuid(motor.id) || !isDatumUuid(motor.mechanismId) || !hasText(motor.name)) {
        throw std::invalid_argument("motor requires UUID identities and a non-empty name");
    }
    if (motor.cadRepresentationObjectId.has_value() && !isDatumUuid(*motor.cadRepresentationObjectId)) {
        throw std::invalid_argument("motor CAD representation must be an object UUID");
    }
    if (motor.massKg.has_value() && (!std::isfinite(*motor.massKg) || *motor.massKg <= 0.0)) {
        throw std::invalid_argument("motor massKg must be finite and positive when specified");
    }
    if (motor.inertiaTensorKgM2.has_value()) {
        validateInertiaTensor(*motor.inertiaTensorKgM2);
    }
    if (motor.centerOfMassLocalM.has_value() && !finite(*motor.centerOfMassLocalM)) {
        throw std::invalid_argument("motor local center of mass must contain finite metre values");
    }
    validateOptionalPositive(motor.ratedSpeedRadPerSec, "motor ratedSpeedRadPerSec");
    validateOptionalPositive(motor.maximumSpeedRadPerSec, "motor maximumSpeedRadPerSec");
    validateOptionalPositive(motor.ratedTorqueNm, "motor ratedTorqueNm");
    validateOptionalPositive(motor.peakTorqueNm, "motor peakTorqueNm");
    if (motor.ratedSpeedRadPerSec.has_value() && motor.maximumSpeedRadPerSec.has_value() &&
        *motor.maximumSpeedRadPerSec < *motor.ratedSpeedRadPerSec) {
        throw std::invalid_argument("motor maximum speed must be at least its rated speed");
    }
    if (motor.ratedTorqueNm.has_value() && motor.peakTorqueNm.has_value() &&
        *motor.peakTorqueNm < *motor.ratedTorqueNm) {
        throw std::invalid_argument("motor peak torque must be at least its rated torque");
    }

    double previousSpeed = -1.0;
    for (const DatumMotorTorquePoint& point : motor.torqueCurve) {
        if (!std::isfinite(point.speedRadPerSec) || point.speedRadPerSec < 0.0 ||
            !std::isfinite(point.torqueNm) || point.torqueNm < 0.0 ||
            point.speedRadPerSec <= previousSpeed) {
            throw std::invalid_argument("motor torque curve speeds must increase strictly and values must be finite and non-negative");
        }
        if (motor.maximumSpeedRadPerSec.has_value() &&
            point.speedRadPerSec > *motor.maximumSpeedRadPerSec) {
            throw std::invalid_argument("motor torque curve exceeds the maximum speed");
        }
        if (motor.peakTorqueNm.has_value() && point.torqueNm > *motor.peakTorqueNm) {
            throw std::invalid_argument("motor torque curve exceeds the peak torque");
        }
        previousSpeed = point.speedRadPerSec;
    }

    validateOptionalPositive(motor.electrical.ratedVoltageV, "motor ratedVoltageV");
    validateOptionalPositive(motor.electrical.ratedCurrentA, "motor ratedCurrentA");
    validateOptionalPositive(motor.electrical.peakCurrentA, "motor peakCurrentA");
    if (motor.electrical.ratedCurrentA.has_value() && motor.electrical.peakCurrentA.has_value() &&
        *motor.electrical.peakCurrentA < *motor.electrical.ratedCurrentA) {
        throw std::invalid_argument("motor peak current must be at least its rated current");
    }
    validateOptionalPositive(motor.thermal.thermalResistanceKPerW, "motor thermalResistanceKPerW");
    validateOptionalPositive(motor.thermal.thermalCapacityJPerK, "motor thermalCapacityJPerK");
    validateOptionalPositive(motor.thermal.maximumWindingTemperatureK, "motor maximumWindingTemperatureK");
    if (motor.encoder.has_value() &&
        (!hasText(motor.encoder->type) || motor.encoder->countsPerRevolution == 0)) {
        throw std::invalid_argument("motor encoder requires a type and positive countsPerRevolution");
    }
    for (const DatumMountingReference& reference : motor.mountingReferences) {
        if (!hasText(reference.role) || !isDatumUuid(reference.objectId) ||
            (reference.topologyReferenceId.has_value() && !isDatumUuid(*reference.topologyReferenceId))) {
            throw std::invalid_argument("motor mounting reference requires a role and valid document UUIDs");
        }
    }
    if (motor.supplierData.has_value()) {
        validateOptionalText(motor.supplierData->manufacturer, "motor manufacturer");
        validateOptionalText(motor.supplierData->partNumber, "motor partNumber");
        validateOptionalText(motor.supplierData->datasheetUrl, "motor datasheetUrl");
    }
}

void validateFrame(const DatumCoordinateFrame& frame)
{
    if (!finite(frame.originM) || !std::all_of(frame.orientationWxyz.begin(), frame.orientationWxyz.end(),
                                               [](double value) { return std::isfinite(value); })) {
        throw std::invalid_argument("joint frames must contain finite metre and quaternion values");
    }
    double normSquared = 0.0;
    for (double value : frame.orientationWxyz) normSquared += value * value;
    if (std::abs(normSquared - 1.0) > 1e-9) {
        throw std::invalid_argument("joint frame orientation must be a normalized [w, x, y, z] quaternion");
    }
}

bool hasEvidenceFor(const DatumMechanismBody& body, const std::string& propertyId)
{
    return std::any_of(body.propertyEvidence.begin(), body.propertyEvidence.end(), [&propertyId](const auto& item) {
        return item.propertyId == propertyId;
    });
}

void validateBody(const DatumMechanismBody& body)
{
    if (!isDatumUuid(body.id) || !isDatumUuid(body.cadObjectId) || !hasText(body.name) ||
        !hasText(body.geometrySource.locator)) {
        throw std::invalid_argument("mechanism body requires UUID identities, a name, and a geometry-source locator");
    }
    try {
        (void)datumGeometrySourceKindName(body.geometrySource.kind);
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("mechanism body geometry-source kind is invalid");
    }
    if ((body.geometrySource.kind == DatumGeometrySourceKind::StepImport ||
         body.geometrySource.kind == DatumGeometrySourceKind::SmlibPyScript) &&
        !hasText(body.geometrySource.revision)) {
        throw std::invalid_argument("STEP imports and SMLibPy scripts require a source revision or content hash");
    }
    if (body.massKg.has_value() && (!std::isfinite(*body.massKg) || *body.massKg <= 0.0)) {
        throw std::invalid_argument("body massKg must be finite and positive when specified");
    }
    if (body.centerOfMassLocalM.has_value() && !finite(*body.centerOfMassLocalM)) {
        throw std::invalid_argument("body center of mass must contain finite local metre values");
    }
    if (body.inertiaTensorKgM2.has_value()) validateInertiaTensor(*body.inertiaTensorKgM2);
    if (body.materialDefinitionId.has_value() && !isDatumUuid(*body.materialDefinitionId)) {
        throw std::invalid_argument("body materialDefinitionId must be a UUID");
    }

    std::unordered_set<std::string> evidencedProperties;
    for (const DatumPropertyEvidence& evidence : body.propertyEvidence) {
        if (!hasText(evidence.propertyId) || !hasText(evidence.reference) ||
            !evidencedProperties.insert(evidence.propertyId).second) {
            throw std::invalid_argument("body property evidence requires unique property IDs and source references");
        }
        try {
            (void)datumPropertyEvidenceKindName(evidence.kind);
        } catch (const std::invalid_argument&) {
            throw std::invalid_argument("body property evidence kind is invalid");
        }
        if (evidence.confidence.has_value() &&
            (!std::isfinite(*evidence.confidence) || *evidence.confidence < 0.0 || *evidence.confidence > 1.0)) {
            throw std::invalid_argument("body property evidence confidence must be in [0, 1]");
        }
    }
    if ((body.massKg.has_value() && !hasEvidenceFor(body, "massKg")) ||
        (body.centerOfMassLocalM.has_value() && !hasEvidenceFor(body, "centerOfMassLocalM")) ||
        (body.inertiaTensorKgM2.has_value() && !hasEvidenceFor(body, "inertiaTensorKgM2"))) {
        throw std::invalid_argument("specified body mass properties require matching provenance records");
    }
}

void validateJoint(const DatumJoint& joint)
{
    if (!isDatumUuid(joint.id) || !hasText(joint.name) || !isDatumUuid(joint.bodyAId) ||
        !isDatumUuid(joint.bodyBId) || joint.bodyAId == joint.bodyBId) {
        throw std::invalid_argument("joint requires a UUID, name, and two distinct body UUIDs");
    }
    try {
        (void)datumJointTypeName(joint.type);
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("joint type is invalid");
    }
    validateFrame(joint.frameInBodyA);
    validateFrame(joint.frameInBodyB);
    if ((joint.lowerLimitSi.has_value() && !std::isfinite(*joint.lowerLimitSi)) ||
        (joint.upperLimitSi.has_value() && !std::isfinite(*joint.upperLimitSi)) ||
        (joint.lowerLimitSi.has_value() && joint.upperLimitSi.has_value() &&
         *joint.lowerLimitSi > *joint.upperLimitSi)) {
        throw std::invalid_argument("joint limits must be finite and ordered in SI units");
    }
    if ((joint.lowerLimitSi.has_value() || joint.upperLimitSi.has_value()) &&
        joint.type != DatumJointType::Revolute && joint.type != DatumJointType::Prismatic) {
        throw std::invalid_argument("scalar joint limits are only supported for revolute and prismatic joints");
    }
}

void validateActuator(const DatumActuator& actuator)
{
    if (!isDatumUuid(actuator.id) || !hasText(actuator.name) || !isDatumUuid(actuator.jointId) ||
        (actuator.motorId.has_value() && !isDatumUuid(*actuator.motorId))) {
        throw std::invalid_argument("actuator requires UUID identities and a non-empty name");
    }
    try {
        (void)datumActuatorControlModeName(actuator.mode);
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("actuator control mode is invalid");
    }
    if ((actuator.maximumEffortSi.has_value() &&
         (!std::isfinite(*actuator.maximumEffortSi) || *actuator.maximumEffortSi <= 0.0)) ||
        (actuator.maximumRateSi.has_value() &&
         (!std::isfinite(*actuator.maximumRateSi) || *actuator.maximumRateSi <= 0.0))) {
        throw std::invalid_argument("actuator effort and rate limits must be positive finite SI values");
    }
    double previousTime = -1.0;
    for (const DatumActuatorPoint& point : actuator.trajectory) {
        if (!std::isfinite(point.timeS) || point.timeS < 0.0 || !std::isfinite(point.valueSi) ||
            point.timeS <= previousTime) {
            throw std::invalid_argument("actuator trajectory times must increase and values must be finite SI values");
        }
        previousTime = point.timeS;
    }
}

void validateContactPair(const DatumContactPair& contact)
{
    if (!isDatumUuid(contact.id) || !hasText(contact.name) || !isDatumUuid(contact.bodyAId) ||
        !isDatumUuid(contact.bodyBId) || contact.bodyAId == contact.bodyBId ||
        (contact.surfaceAReferenceId.has_value() && !isDatumUuid(*contact.surfaceAReferenceId)) ||
        (contact.surfaceBReferenceId.has_value() && !isDatumUuid(*contact.surfaceBReferenceId))) {
        throw std::invalid_argument("contact pair requires UUID identities, a name, and two distinct body UUIDs");
    }
    try {
        (void)datumContactLawName(contact.law);
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("contact law is invalid");
    }
    if (contact.frictionCoefficient.has_value() &&
        (!std::isfinite(*contact.frictionCoefficient) || *contact.frictionCoefficient < 0.0)) {
        throw std::invalid_argument("contact friction coefficient must be finite and non-negative");
    }
    if (contact.law == DatumContactLaw::Frictionless && contact.frictionCoefficient.has_value()) {
        throw std::invalid_argument("frictionless contact cannot specify a friction coefficient");
    }
    if (contact.normalStiffnessNPerM.has_value() &&
        (!std::isfinite(*contact.normalStiffnessNPerM) || *contact.normalStiffnessNPerM <= 0.0)) {
        throw std::invalid_argument("contact normal stiffness must be finite and positive when specified");
    }
    if (contact.normalDampingNsPerM.has_value() &&
        (!std::isfinite(*contact.normalDampingNsPerM) || *contact.normalDampingNsPerM < 0.0)) {
        throw std::invalid_argument("contact normal damping must be finite and non-negative when specified");
    }
    if (contact.restitution.has_value() &&
        (!std::isfinite(*contact.restitution) || *contact.restitution < 0.0 || *contact.restitution > 1.0)) {
        throw std::invalid_argument("contact restitution must be in [0, 1]");
    }
}

void validateRequirement(const DatumRequirement& requirement)
{
    if (!isDatumUuid(requirement.id) || !hasText(requirement.name) || !hasText(requirement.metricId) ||
        !hasText(requirement.unit)) {
        throw std::invalid_argument("requirement requires a UUID, name, metric ID, and unit");
    }
    try {
        (void)datumRequirementRelationName(requirement.relation);
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("requirement relation is invalid");
    }
    const auto finiteOptional = [](const std::optional<double>& value) {
        return !value.has_value() || std::isfinite(*value);
    };
    if (!finiteOptional(requirement.minimumSi) || !finiteOptional(requirement.maximumSi) ||
        !finiteOptional(requirement.targetSi) || !finiteOptional(requirement.toleranceSi) ||
        (requirement.toleranceSi.has_value() && *requirement.toleranceSi < 0.0)) {
        throw std::invalid_argument("requirement bounds and tolerance must be finite SI values");
    }
    switch (requirement.relation) {
    case DatumRequirementRelation::LessOrEqual:
        if (!requirement.maximumSi.has_value() || requirement.minimumSi.has_value() ||
            requirement.targetSi.has_value() || requirement.toleranceSi.has_value()) {
            throw std::invalid_argument("less-or-equal requirement must specify only a maximum");
        }
        break;
    case DatumRequirementRelation::GreaterOrEqual:
        if (!requirement.minimumSi.has_value() || requirement.maximumSi.has_value() ||
            requirement.targetSi.has_value() || requirement.toleranceSi.has_value()) {
            throw std::invalid_argument("greater-or-equal requirement must specify only a minimum");
        }
        break;
    case DatumRequirementRelation::EqualWithin:
        if (!requirement.targetSi.has_value() || !requirement.toleranceSi.has_value() ||
            requirement.minimumSi.has_value() || requirement.maximumSi.has_value()) {
            throw std::invalid_argument("equal-within requirement needs a target and tolerance only");
        }
        break;
    case DatumRequirementRelation::InRange:
        if (!requirement.minimumSi.has_value() || !requirement.maximumSi.has_value() ||
            requirement.targetSi.has_value() || requirement.toleranceSi.has_value() ||
            *requirement.minimumSi > *requirement.maximumSi) {
            throw std::invalid_argument("in-range requirement needs an ordered minimum and maximum only");
        }
        break;
    }
}

}  // namespace

std::string datumAxisTypeName(DatumAxisType type)
{
    switch (type) {
    case DatumAxisType::Linear:
        return "linear";
    case DatumAxisType::Rotary:
        return "rotary";
    }
    throw std::invalid_argument("unknown DatumLoop axis type");
}

std::optional<DatumAxisType> datumAxisTypeFromName(const std::string& name)
{
    if (name == "linear") {
        return DatumAxisType::Linear;
    }
    if (name == "rotary") {
        return DatumAxisType::Rotary;
    }
    return std::nullopt;
}

std::string datumGeometrySourceKindName(DatumGeometrySourceKind kind)
{
    switch (kind) {
    case DatumGeometrySourceKind::EmbeddedBRep: return "embedded-brep";
    case DatumGeometrySourceKind::NativeFeatureGraph: return "native-feature-graph";
    case DatumGeometrySourceKind::FreeCADFeature: return "freecad-feature";
    case DatumGeometrySourceKind::SmlibPyScript: return "smlibpy-script";
    case DatumGeometrySourceKind::StepImport: return "step-import";
    }
    throw std::invalid_argument("unknown mechanism geometry source kind");
}

std::optional<DatumGeometrySourceKind> datumGeometrySourceKindFromName(const std::string& name)
{
    if (name == "embedded-brep") return DatumGeometrySourceKind::EmbeddedBRep;
    if (name == "native-feature-graph") return DatumGeometrySourceKind::NativeFeatureGraph;
    if (name == "freecad-feature") return DatumGeometrySourceKind::FreeCADFeature;
    if (name == "smlibpy-script") return DatumGeometrySourceKind::SmlibPyScript;
    if (name == "step-import") return DatumGeometrySourceKind::StepImport;
    return std::nullopt;
}

std::string datumPropertyEvidenceKindName(DatumPropertyEvidenceKind kind)
{
    switch (kind) {
    case DatumPropertyEvidenceKind::UserSpecified: return "user-specified";
    case DatumPropertyEvidenceKind::MaterialDatabase: return "material-database";
    case DatumPropertyEvidenceKind::Estimated: return "estimated";
    case DatumPropertyEvidenceKind::Calibrated: return "calibrated";
    case DatumPropertyEvidenceKind::Measured: return "measured";
    case DatumPropertyEvidenceKind::GeometryDerived: return "geometry-derived";
    }
    throw std::invalid_argument("unknown mechanism property evidence kind");
}

std::optional<DatumPropertyEvidenceKind> datumPropertyEvidenceKindFromName(const std::string& name)
{
    if (name == "user-specified") return DatumPropertyEvidenceKind::UserSpecified;
    if (name == "material-database") return DatumPropertyEvidenceKind::MaterialDatabase;
    if (name == "estimated") return DatumPropertyEvidenceKind::Estimated;
    if (name == "calibrated") return DatumPropertyEvidenceKind::Calibrated;
    if (name == "measured") return DatumPropertyEvidenceKind::Measured;
    if (name == "geometry-derived") return DatumPropertyEvidenceKind::GeometryDerived;
    return std::nullopt;
}

std::string datumJointTypeName(DatumJointType type)
{
    switch (type) {
    case DatumJointType::Fixed: return "fixed";
    case DatumJointType::Revolute: return "revolute";
    case DatumJointType::Prismatic: return "prismatic";
    case DatumJointType::Spherical: return "spherical";
    case DatumJointType::Universal: return "universal";
    }
    throw std::invalid_argument("unknown mechanism joint type");
}

std::optional<DatumJointType> datumJointTypeFromName(const std::string& name)
{
    if (name == "fixed") return DatumJointType::Fixed;
    if (name == "revolute") return DatumJointType::Revolute;
    if (name == "prismatic") return DatumJointType::Prismatic;
    if (name == "spherical") return DatumJointType::Spherical;
    if (name == "universal") return DatumJointType::Universal;
    return std::nullopt;
}

std::string datumActuatorControlModeName(DatumActuatorControlMode mode)
{
    switch (mode) {
    case DatumActuatorControlMode::Position: return "position";
    case DatumActuatorControlMode::Velocity: return "velocity";
    case DatumActuatorControlMode::Effort: return "effort";
    }
    throw std::invalid_argument("unknown actuator control mode");
}

std::optional<DatumActuatorControlMode> datumActuatorControlModeFromName(const std::string& name)
{
    if (name == "position") return DatumActuatorControlMode::Position;
    if (name == "velocity") return DatumActuatorControlMode::Velocity;
    if (name == "effort") return DatumActuatorControlMode::Effort;
    return std::nullopt;
}

std::string datumContactLawName(DatumContactLaw law)
{
    switch (law) {
    case DatumContactLaw::Frictionless: return "frictionless";
    case DatumContactLaw::Coulomb: return "coulomb";
    }
    throw std::invalid_argument("unknown contact law");
}

std::optional<DatumContactLaw> datumContactLawFromName(const std::string& name)
{
    if (name == "frictionless") return DatumContactLaw::Frictionless;
    if (name == "coulomb") return DatumContactLaw::Coulomb;
    return std::nullopt;
}

std::string datumRequirementRelationName(DatumRequirementRelation relation)
{
    switch (relation) {
    case DatumRequirementRelation::LessOrEqual: return "less-or-equal";
    case DatumRequirementRelation::GreaterOrEqual: return "greater-or-equal";
    case DatumRequirementRelation::EqualWithin: return "equal-within";
    case DatumRequirementRelation::InRange: return "in-range";
    }
    throw std::invalid_argument("unknown requirement relation");
}

std::optional<DatumRequirementRelation> datumRequirementRelationFromName(const std::string& name)
{
    if (name == "less-or-equal") return DatumRequirementRelation::LessOrEqual;
    if (name == "greater-or-equal") return DatumRequirementRelation::GreaterOrEqual;
    if (name == "equal-within") return DatumRequirementRelation::EqualWithin;
    if (name == "in-range") return DatumRequirementRelation::InRange;
    return std::nullopt;
}

const MechanismSpec* DatumEngineeringModel::mechanism(const std::string& id) const noexcept
{
    const auto found = std::find_if(mechanisms.begin(), mechanisms.end(), [&id](const auto& item) {
        return item.id == id;
    });
    return found == mechanisms.end() ? nullptr : &*found;
}

const DatumMotor* DatumEngineeringModel::motor(const std::string& id) const noexcept
{
    const auto found = std::find_if(motors.begin(), motors.end(), [&id](const auto& item) {
        return item.id == id;
    });
    return found == motors.end() ? nullptr : &*found;
}

void DatumEngineeringModel::validate() const
{
    std::unordered_set<std::string> ids;
    std::unordered_map<std::string, const MechanismSpec*> mechanismsById;
    std::unordered_map<std::string, const DatumMotor*> motorsById;
    ids.reserve(mechanisms.size() + motors.size() + structuralStudies.size());

    for (const MechanismSpec& mechanism : mechanisms) {
        if (!isDatumUuid(mechanism.id) || !hasText(mechanism.name) || !ids.insert(mechanism.id).second) {
            throw std::invalid_argument("mechanism requires a unique UUID and non-empty name");
        }
        if (mechanism.specVersion != 1) {
            throw std::invalid_argument("unsupported MechanismSpec version");
        }
        mechanismsById.emplace(mechanism.id, &mechanism);
        std::unordered_set<std::string> bodyObjectIds;
        for (const std::string& bodyId : mechanism.bodyObjectIds) {
            if (!isDatumUuid(bodyId) || !bodyObjectIds.insert(bodyId).second) {
                throw std::invalid_argument("mechanism body object UUIDs must be valid and unique");
            }
        }
        std::unordered_set<std::string> bodyIds;
        std::unordered_set<std::string> definitionObjectIds;
        for (const DatumMechanismBody& body : mechanism.bodies) {
            validateBody(body);
            if (!ids.insert(body.id).second || !bodyIds.insert(body.id).second ||
                !bodyObjectIds.contains(body.cadObjectId) || !definitionObjectIds.insert(body.cadObjectId).second) {
                throw std::invalid_argument("mechanism body IDs must be globally unique and map one-to-one to CAD body objects");
            }
        }
        if (!mechanism.bodies.empty() && definitionObjectIds.size() != bodyObjectIds.size()) {
            throw std::invalid_argument("MechanismSpec bodies must define every bodyObjectId exactly once");
        }
        const auto hasBody = [&mechanism, &bodyIds, &bodyObjectIds](const std::string& id) {
            return mechanism.bodies.empty() ? bodyObjectIds.contains(id) : bodyIds.contains(id);
        };
        for (const DatumAxis& axis : mechanism.axes) {
            if (!isDatumUuid(axis.id) || !ids.insert(axis.id).second || !hasText(axis.name) ||
                !isDatumUuid(axis.baseObjectId) || !isDatumUuid(axis.movingObjectId) ||
                axis.baseObjectId == axis.movingObjectId ||
                !bodyObjectIds.contains(axis.baseObjectId) || !bodyObjectIds.contains(axis.movingObjectId)) {
                throw std::invalid_argument("axis requires a unique UUID, name, and two distinct mechanism bodies");
            }
            try {
                (void)datumAxisTypeName(axis.type);
            } catch (const std::invalid_argument&) {
                throw std::invalid_argument("axis type is invalid");
            }
            if (!finite(axis.originInBaseFrameM) || !finite(axis.directionInBaseFrame)) {
                throw std::invalid_argument("axis origin and direction must be finite");
            }
            const double length = std::sqrt(axis.directionInBaseFrame[0] * axis.directionInBaseFrame[0] +
                                            axis.directionInBaseFrame[1] * axis.directionInBaseFrame[1] +
                                            axis.directionInBaseFrame[2] * axis.directionInBaseFrame[2]);
            if (std::abs(length - 1.0) > 1e-9) {
                throw std::invalid_argument("axis direction must be a normalized dimensionless vector");
            }
            if (axis.actuatorMotorId.has_value() && !isDatumUuid(*axis.actuatorMotorId)) {
                throw std::invalid_argument("axis actuator motor id must be a UUID");
            }
        }
        std::unordered_set<std::string> jointIds;
        for (const DatumJoint& joint : mechanism.joints) {
            validateJoint(joint);
            if (!ids.insert(joint.id).second || !jointIds.insert(joint.id).second ||
                !hasBody(joint.bodyAId) || !hasBody(joint.bodyBId)) {
                throw std::invalid_argument("joint IDs must be unique and endpoints must reference mechanism bodies");
            }
        }
        for (const DatumActuator& actuator : mechanism.actuators) {
            validateActuator(actuator);
            if (!ids.insert(actuator.id).second || !jointIds.contains(actuator.jointId)) {
                throw std::invalid_argument("actuator IDs must be unique and each actuator must reference a mechanism joint");
            }
        }
        for (const DatumContactPair& contact : mechanism.contacts) {
            validateContactPair(contact);
            if (!ids.insert(contact.id).second || !hasBody(contact.bodyAId) || !hasBody(contact.bodyBId)) {
                throw std::invalid_argument("contact IDs must be unique and contact pairs must reference mechanism bodies");
            }
        }
        for (const DatumRequirement& requirement : mechanism.requirements) {
            validateRequirement(requirement);
            if (!ids.insert(requirement.id).second) {
                throw std::invalid_argument("requirement IDs must be unique within the engineering model");
            }
        }
    }

    for (const DatumMotor& motor : motors) {
        validateMotor(motor);
        if (!ids.insert(motor.id).second) {
            throw std::invalid_argument("engineering entity UUIDs must be unique within the model");
        }
        if (!mechanismsById.contains(motor.mechanismId)) {
            throw std::invalid_argument("motor mechanismId must reference an existing mechanism");
        }
        motorsById.emplace(motor.id, &motor);
    }

    for (const MechanismSpec& mechanism : mechanisms) {
        for (const DatumAxis& axis : mechanism.axes) {
            if (axis.actuatorMotorId.has_value()) {
                const auto motor = motorsById.find(*axis.actuatorMotorId);
                if (motor == motorsById.end() || motor->second->mechanismId != mechanism.id) {
                    throw std::invalid_argument("axis actuator motor must belong to the same mechanism");
                }
            }
        }
        for (const DatumActuator& actuator : mechanism.actuators) {
            if (actuator.motorId.has_value()) {
                const auto motor = motorsById.find(*actuator.motorId);
                if (motor == motorsById.end() || motor->second->mechanismId != mechanism.id) {
                    throw std::invalid_argument("actuator motor must belong to the same mechanism");
                }
            }
        }
    }
    for (const StructuralAnalysisStudy& study : structuralStudies) {
        study.validate();
        if (!ids.insert(study.id).second) {
            throw std::invalid_argument("structural analysis study UUIDs must be unique within the engineering model");
        }
        for (const BoundaryCondition& condition : study.boundaryConditions) {
            if (!ids.insert(boundaryConditionId(condition)).second) {
                throw std::invalid_argument("physics condition UUIDs must be unique within the engineering model");
            }
        }
        for (const StructuralAnalysisRun& run : study.runs) {
            if (!ids.insert(run.provenance.solverRunId).second) {
                throw std::invalid_argument("solver run UUIDs must be unique within the engineering model");
            }
            for (const FieldDataset& field : run.fields) {
                if (!ids.insert(field.id).second) {
                    throw std::invalid_argument("field dataset UUIDs must be unique within the engineering model");
                }
            }
        }
    }
}

bool MechanismValidationReport::structurallyValid() const noexcept
{
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const MechanismDiagnostic& diagnostic) {
        return diagnostic.code == MechanismDiagnosticCode::InvalidModel;
    });
}

bool MechanismValidationReport::readyForStudy() const noexcept
{
    return structurallyValid() && std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
        return diagnostic.severity == MechanismDiagnosticSeverity::Blocking;
    });
}

MechanismValidationReport validateMechanismForStudy(const DatumEngineeringModel& engineeringModel,
                                                    const std::string& mechanismId,
                                                    const MechanismValidationContext& context)
{
    MechanismValidationReport report;
    report.mechanismId = mechanismId;
    const auto add = [&report](MechanismDiagnosticCode code,
                               MechanismDiagnosticSeverity severity,
                               std::string entityId,
                               std::string message) {
        report.diagnostics.push_back({code, severity, std::move(entityId), std::move(message)});
    };
    try {
        engineeringModel.validate();
    } catch (const std::invalid_argument& error) {
        add(MechanismDiagnosticCode::InvalidModel, MechanismDiagnosticSeverity::Blocking,
            mechanismId, error.what());
        return report;
    }
    const MechanismSpec* mechanism = engineeringModel.mechanism(mechanismId);
    if (mechanism == nullptr) {
        add(MechanismDiagnosticCode::InvalidModel, MechanismDiagnosticSeverity::Blocking,
            mechanismId, "The requested mechanism does not exist in the engineering model.");
        return report;
    }

    const auto contains = [](const std::vector<std::string>& values, const std::string& value) {
        return std::find(values.begin(), values.end(), value) != values.end();
    };
    const bool designOnly = context.studyKind == MechanismStudyKind::Design;
    if (mechanism->bodies.empty()) {
        add(MechanismDiagnosticCode::MissingBodyDefinitions,
            designOnly ? MechanismDiagnosticSeverity::Warning : MechanismDiagnosticSeverity::Blocking,
            mechanismId,
            "Bodies have only legacy CAD-object references; explicit MechanismSpec body definitions are required for studies.");
    }
    for (const std::string& objectId : mechanism->bodyObjectIds) {
        if (!contains(context.documentObjectIds, objectId)) {
            add(MechanismDiagnosticCode::MissingGeometryObject, MechanismDiagnosticSeverity::Blocking,
                objectId, "Mechanism body references a CAD object that is absent from the current document.");
        }
    }

    std::unordered_map<std::string, const DatumMechanismBody*> bodiesById;
    std::unordered_map<std::string, std::string> bodyIdByObjectId;
    std::vector<std::string> bodyIds;
    if (!mechanism->bodies.empty()) {
        for (const DatumMechanismBody& body : mechanism->bodies) {
            bodiesById.emplace(body.id, &body);
            bodyIdByObjectId.emplace(body.cadObjectId, body.id);
            bodyIds.push_back(body.id);
        }
    } else {
        // Version 1-5 projects used the CAD object UUID as the body reference.
        bodyIds = mechanism->bodyObjectIds;
        for (const std::string& objectId : mechanism->bodyObjectIds) bodyIdByObjectId.emplace(objectId, objectId);
    }

    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    for (const std::string& bodyId : bodyIds) adjacency.emplace(bodyId, std::vector<std::string>{});
    std::size_t connectorCount = 0;
    for (const DatumJoint& joint : mechanism->joints) {
        adjacency[joint.bodyAId].push_back(joint.bodyBId);
        adjacency[joint.bodyBId].push_back(joint.bodyAId);
        ++connectorCount;
    }
    // A legacy axis is a single kinematic connection between its two object-backed bodies.
    for (const DatumAxis& axis : mechanism->axes) {
        const auto base = bodyIdByObjectId.find(axis.baseObjectId);
        const auto moving = bodyIdByObjectId.find(axis.movingObjectId);
        if (base != bodyIdByObjectId.end() && moving != bodyIdByObjectId.end()) {
            adjacency[base->second].push_back(moving->second);
            adjacency[moving->second].push_back(base->second);
            ++connectorCount;
        }
    }
    if (bodyIds.size() > 1 && connectorCount == 0 && !designOnly) {
        add(MechanismDiagnosticCode::MissingJoint, MechanismDiagnosticSeverity::Blocking,
            mechanismId, "A multi-body mechanism needs at least one joint before kinematic or physical studies.");
    }
    if (bodyIds.size() > 1 && !designOnly) {
        std::unordered_set<std::string> visited;
        std::vector<std::string> pending{bodyIds.front()};
        while (!pending.empty()) {
            const std::string current = std::move(pending.back());
            pending.pop_back();
            if (!visited.insert(current).second) continue;
            for (const std::string& neighbor : adjacency[current]) pending.push_back(neighbor);
        }
        if (visited.size() != bodyIds.size()) {
            add(MechanismDiagnosticCode::DisconnectedBodies, MechanismDiagnosticSeverity::Blocking,
                mechanismId, "The mechanism joint graph contains bodies disconnected from its first body.");
        }
    }

    const auto bodyPtr = [&bodiesById](const std::string& bodyId) -> const DatumMechanismBody* {
        const auto found = bodiesById.find(bodyId);
        return found == bodiesById.end() ? nullptr : found->second;
    };
    if (context.studyKind == MechanismStudyKind::Dynamics) {
        for (const std::string& bodyId : bodyIds) {
            const DatumMechanismBody* body = bodyPtr(bodyId);
            if (body == nullptr) continue;
            if (!body->massKg.has_value()) {
                add(MechanismDiagnosticCode::MissingMass, MechanismDiagnosticSeverity::Blocking,
                    bodyId, "Dynamic studies require an explicit or derived body mass in kilograms.");
            }
            if (!body->centerOfMassLocalM.has_value()) {
                add(MechanismDiagnosticCode::MissingCenterOfMass, MechanismDiagnosticSeverity::Blocking,
                    bodyId, "Dynamic studies require a center of mass in the body's local metre frame.");
            }
            if (!body->inertiaTensorKgM2.has_value()) {
                add(MechanismDiagnosticCode::MissingInertia, MechanismDiagnosticSeverity::Blocking,
                    bodyId, "Dynamic studies require a symmetric inertia tensor in kg m^2.");
            }
        }
    }
    if (context.studyKind == MechanismStudyKind::Structural) {
        const auto materialPropertyPresent = [](const MaterialDefinition& material,
                                                const std::string& propertyId,
                                                const std::string& unit,
                                                const auto& acceptableValue) {
            return std::any_of(material.propertyModels.begin(), material.propertyModels.end(),
                               [&propertyId, &unit, &acceptableValue](const MaterialPropertyModel& property) {
                if (property.propertyName != propertyId || property.propertyUnit != unit) return false;
                if (property.constantValue.has_value()) return acceptableValue(*property.constantValue);
                return !property.table.empty() && std::all_of(
                    property.table.begin(), property.table.end(), [&acceptableValue](const auto& point) {
                        return acceptableValue(point.propertyValue);
                    });
            });
        };
        for (const std::string& bodyId : bodyIds) {
            const DatumMechanismBody* body = bodyPtr(bodyId);
            if (body == nullptr) continue;
            if (!body->materialDefinitionId.has_value()) {
                add(MechanismDiagnosticCode::MissingMaterialAssignment, MechanismDiagnosticSeverity::Blocking,
                    bodyId, "Structural studies require a material definition assigned to each body.");
                continue;
            }
            const auto material = std::find_if(context.materialDefinitions.begin(), context.materialDefinitions.end(),
                                               [&body](const MaterialDefinition& item) {
                return item.id == *body->materialDefinitionId;
            });
            if (material == context.materialDefinitions.end()) {
                add(MechanismDiagnosticCode::MissingMaterialDefinition, MechanismDiagnosticSeverity::Blocking,
                    bodyId, "The assigned material definition is not present in the project material catalog.");
                continue;
            }
            try {
                material->validate();
            } catch (const std::invalid_argument& error) {
                add(MechanismDiagnosticCode::InvalidMaterialDefinition, MechanismDiagnosticSeverity::Blocking,
                    bodyId, std::string("The assigned material definition is invalid: ") + error.what());
                continue;
            }
            if (!materialPropertyPresent(*material, "elastic.youngs_modulus", "Pa", [](double value) {
                    return std::isfinite(value) && value > 0.0;
                })) {
                add(MechanismDiagnosticCode::MissingMaterialProperty, MechanismDiagnosticSeverity::Blocking,
                    bodyId, "Structural studies require a sourced elastic.youngs_modulus property in Pa.");
            }
            if (!materialPropertyPresent(*material, "elastic.poisson_ratio", "1", [](double value) {
                    return std::isfinite(value) && value > -1.0 && value < 0.5;
                })) {
                add(MechanismDiagnosticCode::MissingMaterialProperty, MechanismDiagnosticSeverity::Blocking,
                    bodyId, "Structural studies require a sourced isotropic elastic.poisson_ratio property in [−1, 0.5).");
            }
        }
    }
    if (context.studyKind == MechanismStudyKind::Contact) {
        if (mechanism->contacts.empty()) {
            add(MechanismDiagnosticCode::MissingContactPair, MechanismDiagnosticSeverity::Blocking,
                mechanismId, "Contact studies require at least one explicitly defined contact pair.");
        }
        for (const DatumContactPair& contact : mechanism->contacts) {
            if (!contact.surfaceAReferenceId.has_value() || !contact.surfaceBReferenceId.has_value()) {
                add(MechanismDiagnosticCode::MissingContactSurface, MechanismDiagnosticSeverity::Blocking,
                    contact.id, "Contact studies require explicit surface references for both contacting bodies.");
            } else {
                const DatumMechanismBody* bodyA = bodyPtr(contact.bodyAId);
                const DatumMechanismBody* bodyB = bodyPtr(contact.bodyBId);
                const auto hasSurface = [&context](const std::string& id, const std::string& objectId) {
                    return std::any_of(context.surfaceReferences.begin(), context.surfaceReferences.end(),
                                       [&id, &objectId](const auto& reference) {
                                           return reference.id == id && reference.cadObjectId == objectId &&
                                                  reference.subshapeType == DatumSubshapeType::Face;
                                       });
                };
                if (bodyA == nullptr || bodyB == nullptr ||
                    !hasSurface(*contact.surfaceAReferenceId, bodyA->cadObjectId) ||
                    !hasSurface(*contact.surfaceBReferenceId, bodyB->cadObjectId)) {
                    add(MechanismDiagnosticCode::InvalidContactSurfaceReference,
                        MechanismDiagnosticSeverity::Blocking, contact.id,
                        "Contact surface references must resolve to faces on their corresponding CAD bodies.");
                }
            }
            if (contact.law == DatumContactLaw::Coulomb && !contact.frictionCoefficient.has_value()) {
                add(MechanismDiagnosticCode::MissingFrictionCoefficient, MechanismDiagnosticSeverity::Blocking,
                    contact.id, "Coulomb contact requires an explicit friction coefficient; unknown values stay unresolved.");
            }
        }
    }
    return report;
}

}  // namespace datumloop
