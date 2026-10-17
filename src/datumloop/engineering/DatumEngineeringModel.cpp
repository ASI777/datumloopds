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
        throw std::invalid_argument("motor inertia tensor must contain finite kg m^2 values");
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
        throw std::invalid_argument("motor inertia tensor must be positive semidefinite");
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

const DatumMechanism* DatumEngineeringModel::mechanism(const std::string& id) const noexcept
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
    std::unordered_map<std::string, const DatumMechanism*> mechanismsById;
    std::unordered_map<std::string, const DatumMotor*> motorsById;
    ids.reserve(mechanisms.size() + motors.size());

    for (const DatumMechanism& mechanism : mechanisms) {
        if (!isDatumUuid(mechanism.id) || !hasText(mechanism.name) || !ids.insert(mechanism.id).second) {
            throw std::invalid_argument("mechanism requires a unique UUID and non-empty name");
        }
        mechanismsById.emplace(mechanism.id, &mechanism);
        std::unordered_set<std::string> bodyIds;
        for (const std::string& bodyId : mechanism.bodyObjectIds) {
            if (!isDatumUuid(bodyId) || !bodyIds.insert(bodyId).second) {
                throw std::invalid_argument("mechanism body object UUIDs must be valid and unique");
            }
        }
        for (const DatumAxis& axis : mechanism.axes) {
            if (!isDatumUuid(axis.id) || !ids.insert(axis.id).second || !hasText(axis.name) ||
                !isDatumUuid(axis.baseObjectId) || !isDatumUuid(axis.movingObjectId) ||
                axis.baseObjectId == axis.movingObjectId ||
                !bodyIds.contains(axis.baseObjectId) || !bodyIds.contains(axis.movingObjectId)) {
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

    for (const DatumMechanism& mechanism : mechanisms) {
        for (const DatumAxis& axis : mechanism.axes) {
            if (axis.actuatorMotorId.has_value()) {
                const auto motor = motorsById.find(*axis.actuatorMotorId);
                if (motor == motorsById.end() || motor->second->mechanismId != mechanism.id) {
                    throw std::invalid_argument("axis actuator motor must belong to the same mechanism");
                }
            }
        }
    }
}

}  // namespace datumloop
