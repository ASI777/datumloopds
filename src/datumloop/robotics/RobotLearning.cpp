#include "datumloop/robotics/RobotLearning.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace datumloop {
namespace {

UnitDimension lengthUnit(std::string symbol = "m")
{
    UnitDimension unit;
    unit.exponents = {0, 1, 0, 0, 0, 0, 0};
    unit.unitSymbol = std::move(symbol);
    return unit;
}

UnitDimension velocityUnit(std::string symbol = "m/s")
{
    UnitDimension unit;
    unit.exponents = {0, 1, -1, 0, 0, 0, 0};
    unit.unitSymbol = std::move(symbol);
    return unit;
}

UnitDimension inverseLengthUnit(std::string symbol = "count/m")
{
    UnitDimension unit;
    unit.exponents = {0, -1, 0, 0, 0, 0, 0};
    unit.unitSymbol = std::move(symbol);
    return unit;
}

UnitDimension momentOfInertiaUnit(std::string symbol = "kg*m^2")
{
    UnitDimension unit;
    unit.exponents = {1, 2, 0, 0, 0, 0, 0};
    unit.unitSymbol = std::move(symbol);
    return unit;
}

bool finite(double value) { return std::isfinite(value); }

void requireText(const std::string& value, const char* message)
{
    if (value.empty()) throw std::invalid_argument(message);
}

void requireUnit(const UnitDimension& actual, const UnitDimension& expected, const char* message)
{
    actual.validate();
    if (!actual.sameDimension(expected) || actual.unitSymbol != expected.unitSymbol) {
        throw std::invalid_argument(message);
    }
}

std::string xmlEscape(const std::string& text)
{
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text) {
        switch (character) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '\"': escaped += "&quot;"; break;
        case '\'': escaped += "&apos;"; break;
        default: escaped += character; break;
        }
    }
    return escaped;
}

std::string number(double value)
{
    std::ostringstream stream;
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return stream.str();
}

Vector3 rotateByQuaternion(const std::array<double, 4>& quaternionWxyz, const Vector3& vector)
{
    const double w = quaternionWxyz[0];
    const double x = quaternionWxyz[1];
    const double y = quaternionWxyz[2];
    const double z = quaternionWxyz[3];
    const Vector3 qVector{x, y, z};
    const double vectorDot = qVector[0] * vector[0] + qVector[1] * vector[1] + qVector[2] * vector[2];
    const double quaternionVectorNormSquared = x * x + y * y + z * z;
    const Vector3 cross{qVector[1] * vector[2] - qVector[2] * vector[1],
                        qVector[2] * vector[0] - qVector[0] * vector[2],
                        qVector[0] * vector[1] - qVector[1] * vector[0]};
    Vector3 result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = 2.0 * vectorDot * qVector[index] +
                        (w * w - quaternionVectorNormSquared) * vector[index] +
                        2.0 * w * cross[index];
    }
    return result;
}

}  // namespace

void EvidenceReference::validate() const
{
    requireText(id, "evidence reference requires an id");
    requireText(description, "evidence reference requires a description");
    switch (kind) {
    case EvidenceKind::PublishedReference:
    case EvidenceKind::ManufacturerDatasheet:
    case EvidenceKind::Measurement:
    case EvidenceKind::SyntheticFixture:
    case EvidenceKind::ExplicitIdealization:
    case EvidenceKind::AgentDerivation: return;
    }
    throw std::invalid_argument("unknown evidence kind");
}

void SourcedScalar::validate() const
{
    if (!finite(value)) throw std::invalid_argument("sourced scalar must be finite");
    dimension.validate();
    evidence.validate();
}

void SourcedVector3::validate() const
{
    for (const double entry : value) {
        if (!finite(entry)) throw std::invalid_argument("sourced vector entries must be finite");
    }
    dimension.validate();
    evidence.validate();
}

void RobotFramePose::validate() const
{
    for (const double entry : originWorldM) {
        if (!finite(entry)) throw std::invalid_argument("robot base-frame origin must be finite");
    }
    double normSquared = 0.0;
    for (const double entry : orientationWorldWxyz) {
        if (!finite(entry)) throw std::invalid_argument("robot base-frame quaternion must be finite");
        normSquared += entry * entry;
    }
    if (std::abs(normSquared - 1.0) > 1e-10) {
        throw std::invalid_argument("robot base-frame quaternion must be normalized");
    }
    evidence.validate();
}

void SensorNoiseModel::validate(const UnitDimension& outputDimension) const
{
    if (distribution == NoiseDistribution::None) {
        if (standardDeviation) throw std::invalid_argument("noise-free sensor cannot declare a standard deviation");
        return;
    }
    if (distribution != NoiseDistribution::Gaussian || !standardDeviation) {
        throw std::invalid_argument("Gaussian sensor noise requires a sourced standard deviation");
    }
    standardDeviation->validate();
    if (standardDeviation->value <= 0.0 ||
        !standardDeviation->dimension.sameDimension(outputDimension) ||
        standardDeviation->dimension.unitSymbol != outputDimension.unitSymbol) {
        throw std::invalid_argument("sensor noise standard deviation must be positive and use the output unit");
    }
}

void SensorDefinition::validate() const
{
    requireText(id, "sensor requires an id");
    requireText(name, "sensor requires a name");
    if (!finite(updatePeriodS) || updatePeriodS <= 0.0 || !finite(latencyS) || latencyS < 0.0) {
        throw std::invalid_argument("sensor period must be positive and latency must be non-negative");
    }
    evidence.validate();
    if (type == SensorType::Encoder) {
        if (!countsPerMeter) throw std::invalid_argument("linear encoder requires a sourced counts-per-meter value");
        countsPerMeter->validate();
        requireUnit(countsPerMeter->dimension, inverseLengthUnit(), "linear encoder resolution must use count/m");
        if (countsPerMeter->value <= 0.0) throw std::invalid_argument("linear encoder resolution must be positive");
        if (imageWidthPixels != 0 || imageHeightPixels != 0 || horizontalFieldOfViewRad != 0.0) {
            throw std::invalid_argument("encoder cannot declare camera metadata");
        }
        noise.validate(lengthUnit());
        return;
    }
    if (type == SensorType::CameraPlaceholder) {
        if (countsPerMeter || imageWidthPixels <= 0 || imageHeightPixels <= 0 ||
            !finite(horizontalFieldOfViewRad) || horizontalFieldOfViewRad <= 0.0 || horizontalFieldOfViewRad >= 3.14159265358979323846) {
            throw std::invalid_argument("camera placeholder requires dimensions and a valid horizontal field of view");
        }
        if (noise.distribution != NoiseDistribution::None || noise.standardDeviation) {
            throw std::invalid_argument("camera placeholder does not synthesize pixel noise or image data");
        }
        return;
    }
    throw std::invalid_argument("unknown sensor type");
}

void ActuatorDefinition::validate() const
{
    requireText(id, "actuator requires an id");
    requireText(name, "actuator requires a name");
    requireText(axisId, "actuator requires an axis id");
    maximumEffort.validate();
    if (maximumEffort.value <= 0.0 || maximumEffort.dimension.unitSymbol != effortUnit) {
        throw std::invalid_argument("actuator effort limit must be positive and match its declared unit");
    }
    requireText(effortUnit, "actuator requires an effort unit");
    if (datumMotorId && datumMotorId->empty()) throw std::invalid_argument("optional motor id cannot be empty");
}

const SensorDefinition* RobotModel::sensor(const std::string& sensorId) const noexcept
{
    const auto found = std::find_if(sensors.begin(), sensors.end(), [&sensorId](const auto& item) {
        return item.id == sensorId;
    });
    return found == sensors.end() ? nullptr : &*found;
}

void RobotModel::validateAgainst(const DatumEngineeringModel& engineering) const
{
    engineering.validate();
    requireText(id, "robot model requires an id");
    requireText(name, "robot model requires a name");
    requireText(mechanismId, "robot model requires a mechanism id");
    requireText(axisId, "robot model requires an axis id");
    baseFrameInWorld.validate();
    actuator.validate();
    if (actuator.axisId != axisId) throw std::invalid_argument("robot actuator must target the selected axis");
    const DatumMechanism* mechanism = engineering.mechanism(mechanismId);
    if (mechanism == nullptr) throw std::invalid_argument("robot model references an unknown DatumLoop mechanism");
    const auto axis = std::find_if(mechanism->axes.begin(), mechanism->axes.end(), [this](const auto& candidate) {
        return candidate.id == axisId;
    });
    if (axis == mechanism->axes.end()) throw std::invalid_argument("robot model references an unknown mechanism axis");
    if (axis->type != DatumAxisType::Linear) throw std::invalid_argument("initial robot environment requires a linear mechanism axis");
    if (actuator.datumMotorId && axis->actuatorMotorId != actuator.datumMotorId) {
        throw std::invalid_argument("robot actuator motor reference does not match the selected DatumLoop axis");
    }
    std::unordered_set<std::string> sensorIds;
    for (const SensorDefinition& item : sensors) {
        item.validate();
        if (!sensorIds.insert(item.id).second) throw std::invalid_argument("robot sensor ids must be unique");
    }
}

void ObservationSpec::validate(const RobotModel& robot) const
{
    if (sensorIds.empty()) throw std::invalid_argument("observation spec must expose at least one sensor");
    std::unordered_set<std::string> unique;
    for (const std::string& id : sensorIds) {
        if (robot.sensor(id) == nullptr || !unique.insert(id).second) {
            throw std::invalid_argument("observation spec must reference unique sensors in the robot model");
        }
    }
}

void ActionSpec::validate(const ActuatorDefinition& actuator) const
{
    if (actuatorId != actuator.id) throw std::invalid_argument("action spec actuator id does not match the robot model");
    minimumCommand.validate();
    maximumCommand.validate();
    if (!minimumCommand.dimension.sameDimension(maximumCommand.dimension) ||
        minimumCommand.dimension.unitSymbol != maximumCommand.dimension.unitSymbol ||
        minimumCommand.dimension.unitSymbol != actuator.effortUnit ||
        minimumCommand.value >= maximumCommand.value ||
        minimumCommand.value < -actuator.maximumEffort.value ||
        maximumCommand.value > actuator.maximumEffort.value) {
        throw std::invalid_argument("action bounds must be ordered and within the sourced actuator limit");
    }
}

void UncertainParameter::validate() const
{
    requireText(id, "uncertain parameter requires an id");
    dimension.validate();
    evidence.validate();
    if (parameters.size() != 2 || !finite(parameters[0]) || !finite(parameters[1])) {
        throw std::invalid_argument("uncertainty distribution requires two finite parameters");
    }
    if (distribution == UncertaintyDistribution::Uniform && parameters[0] >= parameters[1]) {
        throw std::invalid_argument("uniform distribution bounds must be ordered");
    }
    if (distribution == UncertaintyDistribution::Gaussian && parameters[1] <= 0.0) {
        throw std::invalid_argument("Gaussian distribution standard deviation must be positive");
    }
    if (correlationGroup && correlationGroup->empty()) throw std::invalid_argument("correlation group cannot be empty");
}

void RLEnvironmentSpec::validate(const RobotModel& robot, const DatumEngineeringModel& engineering) const
{
    requireText(id, "RL environment requires an id");
    if (robotModelId != robot.id) throw std::invalid_argument("RL environment references a different robot model");
    robot.validateAgainst(engineering);
    if (episodeStepLimit == 0) throw std::invalid_argument("RL episode step limit must be positive");
    timeStep.validate();
    requireUnit(timeStep.dimension, UnitDimension::second(), "environment timestep must use seconds");
    if (timeStep.value <= 0.0) throw std::invalid_argument("environment timestep must be positive");
    movingMass.validate();
    requireUnit(movingMass.dimension, UnitDimension::kilogram(), "moving mass must use kilograms");
    if (movingMass.value <= 0.0) throw std::invalid_argument("moving mass must be positive");
    movingBodyDiagonalInertia.validate();
    requireUnit(movingBodyDiagonalInertia.dimension, momentOfInertiaUnit(), "moving-body inertia must use kg*m^2");
    if (std::any_of(movingBodyDiagonalInertia.value.begin(), movingBodyDiagonalInertia.value.end(),
                    [](double value) { return value <= 0.0; })) {
        throw std::invalid_argument("moving-body principal inertias must be positive");
    }
    initialPosition.validate();
    requireUnit(initialPosition.dimension, lengthUnit(), "initial position must use metres");
    initialVelocity.validate();
    requireUnit(initialVelocity.dimension, velocityUnit(), "initial velocity must use metres per second");
    targetPosition.validate();
    requireUnit(targetPosition.dimension, lengthUnit(), "target position must use metres");
    targetTolerance.validate();
    requireUnit(targetTolerance.dimension, lengthUnit(), "target tolerance must use metres");
    if (targetTolerance.value <= 0.0) throw std::invalid_argument("target tolerance must be positive");
    action.validate(robot.actuator);
    observation.validate(robot);
    for (const SensorDefinition& item : robot.sensors) {
        const double periodTicks = item.updatePeriodS / timeStep.value;
        const double latencyTicks = item.latencyS / timeStep.value;
        if (std::abs(periodTicks - std::round(periodTicks)) > 1e-9 ||
            std::abs(latencyTicks - std::round(latencyTicks)) > 1e-9) {
            throw std::invalid_argument("sensor update period and latency must align with the environment timestep");
        }
    }
    for (const UncertainParameter& item : randomization) item.validate();
}

DeterministicMassEnvironment::DeterministicMassEnvironment(const RobotModel& robot,
                                                           const DatumEngineeringModel& engineering,
                                                           RLEnvironmentSpec spec)
    : robot_(robot), engineering_(engineering), spec_(std::move(spec)), random_(spec_.randomSeed)
{
    spec_.validate(robot_, engineering_);
    nextSampleTimeS_.resize(robot_.sensors.size(), 0.0);
    latestMeasurements_.resize(robot_.sensors.size());
    (void)reset();
}

RobotObservation DeterministicMassEnvironment::reset()
{
    random_.seed(spec_.randomSeed);
    timeS_ = 0.0;
    positionM_ = spec_.initialPosition.value;
    velocityMPerS_ = spec_.initialVelocity.value;
    stepCount_ = 0;
    terminated_ = false;
    pendingMeasurements_.clear();
    std::fill(nextSampleTimeS_.begin(), nextSampleTimeS_.end(), 0.0);
    std::fill(latestMeasurements_.begin(), latestMeasurements_.end(), std::nullopt);
    sampleSensors();
    deliverSensors();
    return observation();
}

void DeterministicMassEnvironment::sampleSensors()
{
    for (std::size_t index = 0; index < robot_.sensors.size(); ++index) {
        const SensorDefinition& sensor = robot_.sensors[index];
        while (nextSampleTimeS_[index] <= timeS_ + 1e-12) {
            RobotSensorObservation sample;
            sample.sensorId = sensor.id;
            sample.captureTimeS = nextSampleTimeS_[index];
            sample.deliveryTimeS = sample.captureTimeS + sensor.latencyS;
            if (sensor.type == SensorType::Encoder) {
                const double countsPerMeter = sensor.countsPerMeter->value;
                sample.value = std::round(positionM_ * countsPerMeter) / countsPerMeter;
                sample.unit = "m";
                if (sensor.noise.distribution == NoiseDistribution::Gaussian) {
                    std::normal_distribution<double> noise(0.0, sensor.noise.standardDeviation->value);
                    *sample.value += noise(random_);
                }
                sample.valid = true;
                sample.detail = "Quantized linear encoder sample; noise follows the sourced model when configured.";
            } else {
                sample.imageWidthPixels = sensor.imageWidthPixels;
                sample.imageHeightPixels = sensor.imageHeightPixels;
                sample.frameAvailable = false;
                sample.detail = "Camera placeholder only: no pixel payload or image noise model is generated.";
            }
            pendingMeasurements_.push_back({std::move(sample)});
            nextSampleTimeS_[index] += sensor.updatePeriodS;
        }
    }
}

void DeterministicMassEnvironment::deliverSensors()
{
    for (auto iterator = pendingMeasurements_.begin(); iterator != pendingMeasurements_.end();) {
        if (iterator->measurement.deliveryTimeS <= timeS_ + 1e-12) {
            const auto found = std::find_if(robot_.sensors.begin(), robot_.sensors.end(), [&iterator](const auto& sensor) {
                return sensor.id == iterator->measurement.sensorId;
            });
            if (found != robot_.sensors.end()) {
                const auto index = static_cast<std::size_t>(std::distance(robot_.sensors.begin(), found));
                latestMeasurements_[index] = iterator->measurement;
            }
            iterator = pendingMeasurements_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

RobotObservation DeterministicMassEnvironment::observation() const
{
    RobotObservation result;
    result.timeS = timeS_;
    for (const std::string& sensorId : spec_.observation.sensorIds) {
        const auto index = static_cast<std::size_t>(std::distance(robot_.sensors.begin(),
            std::find_if(robot_.sensors.begin(), robot_.sensors.end(), [&sensorId](const auto& sensor) {
                return sensor.id == sensorId;
            })));
        if (index < latestMeasurements_.size() && latestMeasurements_[index]) {
            result.sensors.push_back(*latestMeasurements_[index]);
        } else {
            const SensorDefinition* sensor = robot_.sensor(sensorId);
            RobotSensorObservation unavailable;
            unavailable.sensorId = sensorId;
            unavailable.unit = sensor->type == SensorType::Encoder ? "m" : "";
            unavailable.imageWidthPixels = sensor->imageWidthPixels;
            unavailable.imageHeightPixels = sensor->imageHeightPixels;
            unavailable.detail = sensor->type == SensorType::CameraPlaceholder
                ? "Camera placeholder has not delivered a frame; image data is not implemented."
                : "Encoder has not delivered a sample.";
            result.sensors.push_back(std::move(unavailable));
        }
    }
    return result;
}

RLEnvironmentTransition DeterministicMassEnvironment::step(const RobotAction& action)
{
    if (terminated_ || stepCount_ >= spec_.episodeStepLimit) {
        throw std::logic_error("RL environment episode has ended; call reset before stepping again");
    }
    if (action.actuatorId != spec_.action.actuatorId || action.unit != spec_.action.minimumCommand.dimension.unitSymbol ||
        !finite(action.value) || action.value < spec_.action.minimumCommand.value ||
        action.value > spec_.action.maximumCommand.value) {
        throw std::invalid_argument("RL action does not satisfy the declared actuator contract");
    }
    const double acceleration = action.value / spec_.movingMass.value;
    const double deltaTime = spec_.timeStep.value;
    positionM_ += velocityMPerS_ * deltaTime + 0.5 * acceleration * deltaTime * deltaTime;
    velocityMPerS_ += acceleration * deltaTime;
    timeS_ += deltaTime;
    ++stepCount_;
    sampleSensors();
    deliverSensors();

    const double positionErrorM = spec_.targetPosition.value - positionM_;
    terminated_ = std::abs(positionErrorM) <= spec_.targetTolerance.value;
    const bool truncated = stepCount_ >= spec_.episodeStepLimit && !terminated_;
    return {observation(), -std::abs(positionErrorM) / spec_.targetTolerance.value, terminated_, truncated};
}

void runControllerCycle(const StateInterface& state, CommandInterface& command, const ControllerFunction& controller)
{
    if (!controller) throw std::invalid_argument("controller callback is empty");
    command.writeCommand(controller(state.readState()));
}

SimulatedRobotIo::SimulatedRobotIo(ControlState initialState,
                                   std::string actuatorId,
                                   std::string effortUnit,
                                   double minimumEffort,
                                   double maximumEffort)
    : state_(std::move(initialState)), actuatorId_(std::move(actuatorId)), effortUnit_(std::move(effortUnit)),
      minimumEffort_(minimumEffort), maximumEffort_(maximumEffort)
{
    requireText(state_.axisId, "simulated state requires an axis id");
    requireText(actuatorId_, "simulated I/O requires an actuator id");
    requireText(effortUnit_, "simulated I/O requires an effort unit");
    if (!finite(minimumEffort_) || !finite(maximumEffort_) || minimumEffort_ >= maximumEffort_) {
        throw std::invalid_argument("simulated actuator limits must be finite and ordered");
    }
}

void SimulatedRobotIo::writeCommand(const ActuatorCommand& command)
{
    if (command.actuatorId != actuatorId_ || command.unit != effortUnit_ || !finite(command.value) ||
        command.value < minimumEffort_ || command.value > maximumEffort_) {
        throw std::invalid_argument("simulated command violates the configured actuator contract");
    }
    lastCommand_ = command;
}

void SimulatedRobotIo::updateState(ControlState state)
{
    if (state.axisId != state_.axisId || !finite(state.timeS) || !finite(state.position) || !finite(state.velocity)) {
        throw std::invalid_argument("simulated state update must match the axis and contain finite values");
    }
    state_ = std::move(state);
}

void DisabledHardwareCommandInterface::writeCommand(const ActuatorCommand&)
{
    throw std::logic_error("hardware actuator writes are disabled in this DatumLoop slice");
}

std::string exportMuJoCoMjcf(const RobotModel& robot,
                             const DatumEngineeringModel& engineering,
                             const RLEnvironmentSpec& spec)
{
    spec.validate(robot, engineering);
    const DatumMechanism* mechanism = engineering.mechanism(robot.mechanismId);
    const auto axis = std::find_if(mechanism->axes.begin(), mechanism->axes.end(), [&robot](const auto& item) {
        return item.id == robot.axisId;
    });
    const Vector3& direction = axis->directionInBaseFrame;
    const double norm = std::sqrt(direction[0] * direction[0] +
                                  direction[1] * direction[1] +
                                  direction[2] * direction[2]);
    if (!finite(norm) || norm <= 0.0) throw std::invalid_argument("MuJoCo export axis must have a finite direction");
    const double effortLimit = std::max(std::abs(spec.action.minimumCommand.value),
                                        std::abs(spec.action.maximumCommand.value));
    const Vector3 rotatedJointOrigin = rotateByQuaternion(robot.baseFrameInWorld.orientationWorldWxyz,
                                                          axis->originInBaseFrameM);
    const Vector3 bodyOriginWorldM{robot.baseFrameInWorld.originWorldM[0] + rotatedJointOrigin[0],
                                   robot.baseFrameInWorld.originWorldM[1] + rotatedJointOrigin[1],
                                   robot.baseFrameInWorld.originWorldM[2] + rotatedJointOrigin[2]};
    std::ostringstream xml;
    xml << "<mujoco model=\"" << xmlEscape(robot.name) << "\">\n"
        << "  <option timestep=\"" << number(spec.timeStep.value) << "\"/>\n"
        << "  <worldbody>\n"
        << "    <body name=\"moving_body\" pos=\"" << number(bodyOriginWorldM[0]) << ' '
        << number(bodyOriginWorldM[1]) << ' ' << number(bodyOriginWorldM[2]) << "\" quat=\""
        << number(robot.baseFrameInWorld.orientationWorldWxyz[0]) << ' '
        << number(robot.baseFrameInWorld.orientationWorldWxyz[1]) << ' '
        << number(robot.baseFrameInWorld.orientationWorldWxyz[2]) << ' '
        << number(robot.baseFrameInWorld.orientationWorldWxyz[3]) << "\">\n"
        << "      <joint name=\"" << xmlEscape(axis->name) << "\" type=\"slide\" axis=\""
        << number(direction[0] / norm) << ' ' << number(direction[1] / norm) << ' ' << number(direction[2] / norm) << "\"/>\n"
        << "      <inertial pos=\"0 0 0\" mass=\"" << number(spec.movingMass.value)
        << "\" diaginertia=\"" << number(spec.movingBodyDiagonalInertia.value[0]) << ' '
        << number(spec.movingBodyDiagonalInertia.value[1]) << ' '
        << number(spec.movingBodyDiagonalInertia.value[2]) << "\"/>\n"
        << "    </body>\n"
        << "  </worldbody>\n"
        << "  <actuator>\n"
        << "    <motor name=\"" << xmlEscape(robot.actuator.name) << "\" joint=\"" << xmlEscape(axis->name)
        << "\" gear=\"1\" ctrllimited=\"true\" ctrlrange=\""
        << number(-effortLimit) << ' ' << number(effortLimit) << "\"/>\n"
        << "  </actuator>\n"
        << "</mujoco>\n";
    return xml.str();
}

}  // namespace datumloop
