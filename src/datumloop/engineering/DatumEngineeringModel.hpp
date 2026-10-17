#pragma once

#include "datumloop/geometry/GeometryTypes.hpp"

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

struct DatumMechanism {
    std::string id;
    std::string name;
    std::vector<std::string> bodyObjectIds;
    std::vector<DatumAxis> axes;

    friend bool operator==(const DatumMechanism&, const DatumMechanism&) = default;
};

struct DatumMotorTorquePoint {
    double speedRadPerSec = 0.0;
    double torqueNm = 0.0;

    friend bool operator==(const DatumMotorTorquePoint&, const DatumMotorTorquePoint&) = default;
};

// Symmetric inertia tensor ordering: Ixx, Iyy, Izz, Ixy, Ixz, Iyz, in kg m^2.
using DatumInertiaTensorKgM2 = std::array<double, 6>;

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
    std::vector<DatumMechanism> mechanisms;
    std::vector<DatumMotor> motors;

    void validate() const;
    [[nodiscard]] const DatumMechanism* mechanism(const std::string& id) const noexcept;
    [[nodiscard]] const DatumMotor* motor(const std::string& id) const noexcept;

    friend bool operator==(const DatumEngineeringModel&, const DatumEngineeringModel&) = default;
};

}  // namespace datumloop
