#pragma once

#include "datumloop/engineering/DatumEngineeringModel.hpp"
#include "datumloop/knowledge/EquationRegistry.hpp"
#include "datumloop/numerics/StructuralReducedModel.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace datumloop {

enum class EvidenceKind {
    PublishedReference,
    ManufacturerDatasheet,
    Measurement,
    SyntheticFixture,
    ExplicitIdealization,
    AgentDerivation,
};

struct EvidenceReference {
    EvidenceKind kind = EvidenceKind::SyntheticFixture;
    std::string id;
    std::string description;

    void validate() const;
};

struct SourcedScalar {
    double value = 0.0;
    UnitDimension dimension;
    EvidenceReference evidence;

    void validate() const;
};

struct SourcedVector3 {
    Vector3 value{0.0, 0.0, 0.0};
    UnitDimension dimension;
    EvidenceReference evidence;

    void validate() const;
};

struct RobotFramePose {
    Vector3 originWorldM{0.0, 0.0, 0.0};
    // Quaternion ordering is scalar-first: [w, x, y, z].
    std::array<double, 4> orientationWorldWxyz{1.0, 0.0, 0.0, 0.0};
    EvidenceReference evidence;

    void validate() const;
};

enum class SensorType { Encoder, CameraPlaceholder };
enum class NoiseDistribution { None, Gaussian };

struct SensorNoiseModel {
    NoiseDistribution distribution = NoiseDistribution::None;
    std::optional<SourcedScalar> standardDeviation;

    void validate(const UnitDimension& outputDimension) const;
};

struct SensorDefinition {
    std::string id;
    std::string name;
    SensorType type = SensorType::Encoder;
    double updatePeriodS = 0.0;
    double latencyS = 0.0;
    EvidenceReference evidence;
    SensorNoiseModel noise;
    std::optional<SourcedScalar> countsPerMeter;
    int imageWidthPixels = 0;
    int imageHeightPixels = 0;
    double horizontalFieldOfViewRad = 0.0;

    void validate() const;
};

struct ActuatorDefinition {
    std::string id;
    std::string name;
    std::string axisId;
    std::string effortUnit;
    SourcedScalar maximumEffort;
    std::optional<std::string> datumMotorId;

    void validate() const;
};

struct RobotModel {
    std::string id;
    std::string name;
    std::string mechanismId;
    std::string axisId;
    RobotFramePose baseFrameInWorld;
    ActuatorDefinition actuator;
    std::vector<SensorDefinition> sensors;

    void validateAgainst(const DatumEngineeringModel& engineering) const;
    [[nodiscard]] const SensorDefinition* sensor(const std::string& sensorId) const noexcept;
};

struct ObservationSpec {
    std::vector<std::string> sensorIds;

    void validate(const RobotModel& robot) const;
};

struct ActionSpec {
    std::string actuatorId;
    SourcedScalar minimumCommand;
    SourcedScalar maximumCommand;

    void validate(const ActuatorDefinition& actuator) const;
};

enum class UncertaintyDistribution { Uniform, Gaussian };

struct UncertainParameter {
    std::string id;
    UnitDimension dimension;
    UncertaintyDistribution distribution = UncertaintyDistribution::Uniform;
    // Uniform: [lower, upper]. Gaussian: [mean, standard deviation].
    std::vector<double> parameters;
    EvidenceReference evidence;
    std::optional<std::string> correlationGroup;

    void validate() const;
};

struct RLEnvironmentSpec {
    std::string id;
    std::string robotModelId;
    SourcedScalar timeStep;
    std::uint64_t episodeStepLimit = 0;
    SourcedScalar movingMass;
    SourcedVector3 movingBodyDiagonalInertia;
    SourcedScalar initialPosition;
    SourcedScalar initialVelocity;
    SourcedScalar jointLowerPosition;
    SourcedScalar jointUpperPosition;
    SourcedScalar targetPosition;
    SourcedScalar targetTolerance;
    std::uint64_t randomSeed = 0;
    ActionSpec action;
    ObservationSpec observation;
    std::vector<UncertainParameter> randomization;

    void validate(const RobotModel& robot, const DatumEngineeringModel& engineering) const;
};

struct RobotSensorObservation {
    std::string sensorId;
    bool valid = false;
    std::optional<double> value;
    std::string unit;
    double captureTimeS = 0.0;
    double deliveryTimeS = 0.0;
    bool frameAvailable = false;
    int imageWidthPixels = 0;
    int imageHeightPixels = 0;
    std::string detail;
};

struct RobotObservation {
    double timeS = 0.0;
    std::vector<RobotSensorObservation> sensors;
    struct StructuralResponse {
        std::string romId;
        Vector3 appliedForceDirectionUnit{0.0, 0.0, 0.0};
        double appliedForceMagnitudeN = 0.0;
        double youngsModulusPa = 0.0;
        double tipDeflectionMagnitudeM = 0.0;
        double effectiveStiffnessNPerM = 0.0;
    };
    std::optional<StructuralResponse> structuralResponse;
};

// Adds a bounded, static flexible response to an observation supplied by a
// rigid-body backend such as MuJoCo. Existing rigid sensor values are kept.
void addStructuralResponseObservation(RobotObservation& observation,
                                      const StructuralElasticityRom& rom,
                                      const Vector3& appliedForceN,
                                      double youngsModulusPa);

struct RobotAction {
    std::string actuatorId;
    double value = 0.0;
    std::string unit;
};

struct RLEnvironmentTransition {
    RobotObservation observation;
    double reward = 0.0;
    bool terminated = false;
    bool truncated = false;
};

class DeterministicMassEnvironment final {
public:
    DeterministicMassEnvironment(const RobotModel& robot,
                                 const DatumEngineeringModel& engineering,
                                 RLEnvironmentSpec spec);

    [[nodiscard]] RobotObservation reset();
    [[nodiscard]] RLEnvironmentTransition step(const RobotAction& action);
    [[nodiscard]] const RLEnvironmentSpec& spec() const noexcept { return spec_; }

private:
    struct PendingMeasurement {
        RobotSensorObservation measurement;
    };

    void sampleSensors();
    void deliverSensors();
    [[nodiscard]] RobotObservation observation() const;

    const RobotModel& robot_;
    const DatumEngineeringModel& engineering_;
    RLEnvironmentSpec spec_;
    std::mt19937_64 random_;
    std::vector<double> nextSampleTimeS_;
    std::vector<std::optional<RobotSensorObservation>> latestMeasurements_;
    std::vector<PendingMeasurement> pendingMeasurements_;
    double timeS_ = 0.0;
    double positionM_ = 0.0;
    double velocityMPerS_ = 0.0;
    std::uint64_t stepCount_ = 0;
    bool terminated_ = false;
};

struct ControlState {
    std::string axisId;
    double timeS = 0.0;
    double position = 0.0;
    double velocity = 0.0;
    std::string positionUnit;
    std::string velocityUnit;
};

struct ActuatorCommand {
    std::string actuatorId;
    double value = 0.0;
    std::string unit;
};

class StateInterface {
public:
    virtual ~StateInterface() = default;
    [[nodiscard]] virtual ControlState readState() const = 0;
};

class CommandInterface {
public:
    virtual ~CommandInterface() = default;
    virtual void writeCommand(const ActuatorCommand& command) = 0;
};

using ControllerFunction = std::function<ActuatorCommand(const ControlState&)>;
void runControllerCycle(const StateInterface& state, CommandInterface& command, const ControllerFunction& controller);

class SimulatedRobotIo final : public StateInterface, public CommandInterface {
public:
    SimulatedRobotIo(ControlState initialState,
                     std::string actuatorId,
                     std::string effortUnit,
                     double minimumEffort,
                     double maximumEffort);

    [[nodiscard]] ControlState readState() const override { return state_; }
    void writeCommand(const ActuatorCommand& command) override;
    void updateState(ControlState state);
    [[nodiscard]] const std::optional<ActuatorCommand>& lastCommand() const noexcept { return lastCommand_; }

private:
    ControlState state_;
    std::string actuatorId_;
    std::string effortUnit_;
    double minimumEffort_ = 0.0;
    double maximumEffort_ = 0.0;
    std::optional<ActuatorCommand> lastCommand_;
};

class DisabledHardwareCommandInterface final : public CommandInterface {
public:
    void writeCommand(const ActuatorCommand& command) override;
};

[[nodiscard]] std::string exportMuJoCoMjcf(const RobotModel& robot,
                                           const DatumEngineeringModel& engineering,
                                           const RLEnvironmentSpec& spec);

}  // namespace datumloop
