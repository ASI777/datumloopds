#pragma once

#include "datumloop/core/DatumTopologyReference.hpp"
#include "datumloop/geometry/GeometryTypes.hpp"
#include "datumloop/materials/MaterialModel.hpp"
#include "datumloop/engineering/StructuralAnalysis.hpp"
#include "datumloop/engineering/PartDesign.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace datumloop {

enum class DatumAxisType {
    Linear,
    Rotary,
};

[[nodiscard]] std::string datumAxisTypeName(DatumAxisType type);
[[nodiscard]] std::optional<DatumAxisType> datumAxisTypeFromName(const std::string& name);

struct DatumAxis {
    std::string id;
    std::string name;
    DatumAxisType type = DatumAxisType::Rotary;
    std::string baseObjectId;
    std::string movingObjectId;
    Vector3 originInBaseFrameM{0.0, 0.0, 0.0};
    Vector3 directionInBaseFrame{0.0, 0.0, 1.0};
    std::optional<std::string> actuatorMotorId;

    friend bool operator==(const DatumAxis&, const DatumAxis&) = default;
};

// Symmetric inertia tensor ordering: Ixx, Iyy, Izz, Ixy, Ixz, Iyz, in kg m^2.
using DatumInertiaTensorKgM2 = std::array<double, 6>;

enum class DatumGeometrySourceKind {
    EmbeddedBRep,
    NativeFeatureGraph,
    FreeCADFeature,
    SmlibPyScript,
    StepImport,
};

[[nodiscard]] std::string datumGeometrySourceKindName(DatumGeometrySourceKind kind);
[[nodiscard]] std::optional<DatumGeometrySourceKind> datumGeometrySourceKindFromName(const std::string& name);

struct DatumGeometrySource {
    DatumGeometrySourceKind kind = DatumGeometrySourceKind::EmbeddedBRep;
    // Document object ID, feature ID, relative script path, or imported asset URI.
    std::string locator;
    // Content hash or source revision when one is available.
    std::string revision;

    friend bool operator==(const DatumGeometrySource&, const DatumGeometrySource&) = default;
};

enum class DatumPropertyEvidenceKind {
    UserSpecified,
    MaterialDatabase,
    Estimated,
    Calibrated,
    Measured,
    GeometryDerived,
};

[[nodiscard]] std::string datumPropertyEvidenceKindName(DatumPropertyEvidenceKind kind);
[[nodiscard]] std::optional<DatumPropertyEvidenceKind> datumPropertyEvidenceKindFromName(const std::string& name);

struct DatumPropertyEvidence {
    std::string propertyId;
    DatumPropertyEvidenceKind kind = DatumPropertyEvidenceKind::UserSpecified;
    std::string reference;
    std::string revision;
    std::optional<double> confidence;

    friend bool operator==(const DatumPropertyEvidence&, const DatumPropertyEvidence&) = default;
};

struct DatumMechanismBody {
    std::string id;
    std::string name;
    std::string cadObjectId;
    DatumGeometrySource geometrySource;
    std::optional<double> massKg;
    std::optional<Vector3> centerOfMassLocalM;
    std::optional<DatumInertiaTensorKgM2> inertiaTensorKgM2;
    std::optional<std::string> materialDefinitionId;
    std::vector<DatumPropertyEvidence> propertyEvidence;

    friend bool operator==(const DatumMechanismBody&, const DatumMechanismBody&) = default;
};

struct DatumCoordinateFrame {
    Vector3 originM{0.0, 0.0, 0.0};
    // Unit quaternion in [w, x, y, z] order; maps frame-local vectors to the parent frame.
    std::array<double, 4> orientationWxyz{1.0, 0.0, 0.0, 0.0};

    friend bool operator==(const DatumCoordinateFrame&, const DatumCoordinateFrame&) = default;
};

enum class DatumJointType {
    Fixed,
    Revolute,
    Prismatic,
    Spherical,
    Universal,
};

[[nodiscard]] std::string datumJointTypeName(DatumJointType type);
[[nodiscard]] std::optional<DatumJointType> datumJointTypeFromName(const std::string& name);

struct DatumJoint {
    std::string id;
    std::string name;
    DatumJointType type = DatumJointType::Fixed;
    std::string bodyAId;
    std::string bodyBId;
    DatumCoordinateFrame frameInBodyA;
    DatumCoordinateFrame frameInBodyB;
    // Revolute limits are radians; prismatic limits are metres.
    std::optional<double> lowerLimitSi;
    std::optional<double> upperLimitSi;

    friend bool operator==(const DatumJoint&, const DatumJoint&) = default;
};

enum class DatumActuatorControlMode {
    Position,
    Velocity,
    Effort,
};

[[nodiscard]] std::string datumActuatorControlModeName(DatumActuatorControlMode mode);
[[nodiscard]] std::optional<DatumActuatorControlMode> datumActuatorControlModeFromName(const std::string& name);

struct DatumActuatorPoint {
    double timeS = 0.0;
    // SI coordinate, coordinate rate, or effort according to mode and joint type.
    double valueSi = 0.0;

    friend bool operator==(const DatumActuatorPoint&, const DatumActuatorPoint&) = default;
};

struct DatumActuator {
    std::string id;
    std::string name;
    std::string jointId;
    DatumActuatorControlMode mode = DatumActuatorControlMode::Position;
    std::optional<std::string> motorId;
    std::vector<DatumActuatorPoint> trajectory;
    std::optional<double> maximumEffortSi;
    std::optional<double> maximumRateSi;

    friend bool operator==(const DatumActuator&, const DatumActuator&) = default;
};

enum class DatumContactLaw {
    Frictionless,
    Coulomb,
};

[[nodiscard]] std::string datumContactLawName(DatumContactLaw law);
[[nodiscard]] std::optional<DatumContactLaw> datumContactLawFromName(const std::string& name);

struct DatumContactPair {
    std::string id;
    std::string name;
    std::string bodyAId;
    std::string bodyBId;
    std::optional<std::string> surfaceAReferenceId;
    std::optional<std::string> surfaceBReferenceId;
    DatumContactLaw law = DatumContactLaw::Frictionless;
    std::optional<double> frictionCoefficient;
    std::optional<double> normalStiffnessNPerM;
    std::optional<double> normalDampingNsPerM;
    std::optional<double> restitution;

    friend bool operator==(const DatumContactPair&, const DatumContactPair&) = default;
};

enum class DatumRequirementRelation {
    LessOrEqual,
    GreaterOrEqual,
    EqualWithin,
    InRange,
};

[[nodiscard]] std::string datumRequirementRelationName(DatumRequirementRelation relation);
[[nodiscard]] std::optional<DatumRequirementRelation> datumRequirementRelationFromName(const std::string& name);

struct DatumRequirement {
    std::string id;
    std::string name;
    std::string metricId;
    std::string unit;
    DatumRequirementRelation relation = DatumRequirementRelation::LessOrEqual;
    std::optional<double> minimumSi;
    std::optional<double> maximumSi;
    std::optional<double> targetSi;
    std::optional<double> toleranceSi;
    bool hardConstraint = true;

    friend bool operator==(const DatumRequirement&, const DatumRequirement&) = default;
};

struct MechanismSpec {
    std::uint32_t specVersion = 1;
    std::string id;
    std::string name;
    // Compatibility projection retained for existing robot fixtures and v1-v5 files.
    std::vector<std::string> bodyObjectIds;
    std::vector<DatumMechanismBody> bodies;
    std::vector<DatumJoint> joints;
    std::vector<DatumActuator> actuators;
    std::vector<DatumContactPair> contacts;
    std::vector<DatumRequirement> requirements;
    std::vector<DatumAxis> axes;

    friend bool operator==(const MechanismSpec&, const MechanismSpec&) = default;
};

using DatumMechanism = MechanismSpec;

struct DatumMotorTorquePoint {
    double speedRadPerSec = 0.0;
    double torqueNm = 0.0;

    friend bool operator==(const DatumMotorTorquePoint&, const DatumMotorTorquePoint&) = default;
};

struct DatumMotorElectricalModel {
    std::optional<double> ratedVoltageV;
    std::optional<double> ratedCurrentA;
    std::optional<double> peakCurrentA;

    friend bool operator==(const DatumMotorElectricalModel&, const DatumMotorElectricalModel&) = default;
};

struct DatumMotorThermalModel {
    std::optional<double> thermalResistanceKPerW;
    std::optional<double> thermalCapacityJPerK;
    std::optional<double> maximumWindingTemperatureK;

    friend bool operator==(const DatumMotorThermalModel&, const DatumMotorThermalModel&) = default;
};

struct DatumMotorEncoder {
    std::string type;
    std::uint64_t countsPerRevolution = 0;

    friend bool operator==(const DatumMotorEncoder&, const DatumMotorEncoder&) = default;
};

struct DatumMountingReference {
    std::string role;
    std::string objectId;
    std::optional<std::string> topologyReferenceId;

    friend bool operator==(const DatumMountingReference&, const DatumMountingReference&) = default;
};

struct DatumMotorSupplierData {
    std::optional<std::string> manufacturer;
    std::optional<std::string> partNumber;
    std::optional<std::string> datasheetUrl;

    friend bool operator==(const DatumMotorSupplierData&, const DatumMotorSupplierData&) = default;
};

struct DatumMotor {
    std::string id;
    std::string mechanismId;
    std::string name;
    std::optional<std::string> cadRepresentationObjectId;
    std::optional<double> massKg;
    std::optional<DatumInertiaTensorKgM2> inertiaTensorKgM2;
    std::optional<Vector3> centerOfMassLocalM;
    std::optional<double> ratedSpeedRadPerSec;
    std::optional<double> maximumSpeedRadPerSec;
    std::optional<double> ratedTorqueNm;
    std::optional<double> peakTorqueNm;
    std::vector<DatumMotorTorquePoint> torqueCurve;
    DatumMotorElectricalModel electrical;
    DatumMotorThermalModel thermal;
    std::optional<DatumMotorEncoder> encoder;
    std::vector<DatumMountingReference> mountingReferences;
    std::optional<DatumMotorSupplierData> supplierData;

    friend bool operator==(const DatumMotor&, const DatumMotor&) = default;
};

// Engineering values and references are independent of CAD shape storage.
// Missing optional physical data remains unknown; consumers must not infer it
// from a render mesh or silently substitute geometry-derived values.
struct DatumEngineeringModel {
    std::vector<MechanismSpec> mechanisms;
    std::vector<DatumMotor> motors;
    std::vector<StructuralAnalysisStudy> structuralStudies;
    std::vector<PartRequirements> partRequirements;

    void validate() const;
    [[nodiscard]] const MechanismSpec* mechanism(const std::string& id) const noexcept;
    [[nodiscard]] const DatumMotor* motor(const std::string& id) const noexcept;

    friend bool operator==(const DatumEngineeringModel&, const DatumEngineeringModel&) = default;
};

enum class MechanismStudyKind {
    Design,
    Kinematics,
    Dynamics,
    Structural,
    Contact,
};

enum class MechanismDiagnosticCode {
    InvalidModel,
    MissingBodyDefinitions,
    MissingGeometryObject,
    DisconnectedBodies,
    MissingJoint,
    MissingMass,
    MissingCenterOfMass,
    MissingInertia,
    MissingMaterialAssignment,
    MissingMaterialDefinition,
    InvalidMaterialDefinition,
    MissingMaterialProperty,
    MissingContactPair,
    MissingContactSurface,
    InvalidContactSurfaceReference,
    MissingFrictionCoefficient,
};

enum class MechanismDiagnosticSeverity {
    Warning,
    Blocking,
};

struct MechanismDiagnostic {
    MechanismDiagnosticCode code = MechanismDiagnosticCode::InvalidModel;
    MechanismDiagnosticSeverity severity = MechanismDiagnosticSeverity::Blocking;
    std::string entityId;
    std::string message;

    friend bool operator==(const MechanismDiagnostic&, const MechanismDiagnostic&) = default;
};

struct MechanismValidationContext {
    MechanismStudyKind studyKind = MechanismStudyKind::Design;
    std::vector<std::string> documentObjectIds;
    std::vector<MaterialDefinition> materialDefinitions;
    struct SurfaceReference {
        std::string id;
        std::string cadObjectId;
        DatumSubshapeType subshapeType = DatumSubshapeType::Face;

        friend bool operator==(const SurfaceReference&, const SurfaceReference&) = default;
    };
    std::vector<SurfaceReference> surfaceReferences;
};

struct MechanismValidationReport {
    std::string mechanismId;
    std::vector<MechanismDiagnostic> diagnostics;

    [[nodiscard]] bool structurallyValid() const noexcept;
    [[nodiscard]] bool readyForStudy() const noexcept;
};

[[nodiscard]] MechanismValidationReport validateMechanismForStudy(
    const DatumEngineeringModel& engineeringModel,
    const std::string& mechanismId,
    const MechanismValidationContext& context);

}  // namespace datumloop
