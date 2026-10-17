#include "datumloop/robotics/RobotLearning.hpp"

#include <QXmlStreamReader>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void requireThrows(Function&& function, const char* message)
{
    bool threw = false;
    try {
        function();
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, message);
}

datumloop::EvidenceReference fixture(std::string id, std::string description)
{
    return {datumloop::EvidenceKind::SyntheticFixture, std::move(id), std::move(description)};
}

datumloop::UnitDimension length(std::string symbol = "m")
{
    return {{{0, 1, 0, 0, 0, 0, 0}}, std::move(symbol)};
}

datumloop::UnitDimension force()
{
    return datumloop::UnitDimension::newton();
}

datumloop::SourcedScalar scalar(double value, datumloop::UnitDimension dimension, std::string id, std::string description)
{
    return {value, std::move(dimension), fixture(std::move(id), std::move(description))};
}

datumloop::DatumEngineeringModel makeEngineering()
{
    datumloop::DatumMechanism mechanism;
    mechanism.id = "10000000-0000-4000-8000-000000000001";
    mechanism.name = "One-axis fixture";
    mechanism.bodyObjectIds = {"10000000-0000-4000-8000-000000000002", "10000000-0000-4000-8000-000000000003"};
    datumloop::DatumAxis axis;
    axis.id = "10000000-0000-4000-8000-000000000004";
    axis.name = "slider_x";
    axis.type = datumloop::DatumAxisType::Linear;
    axis.baseObjectId = mechanism.bodyObjectIds[0];
    axis.movingObjectId = mechanism.bodyObjectIds[1];
    axis.originInBaseFrameM = {0.1, 0.0, 0.0};
    axis.directionInBaseFrame = {1.0, 0.0, 0.0};
    mechanism.axes.push_back(axis);
    datumloop::DatumEngineeringModel model;
    model.mechanisms.push_back(mechanism);
    model.validate();
    return model;
}

datumloop::RobotModel makeRobot()
{
    datumloop::RobotModel robot;
    robot.id = "robot.one-axis.fixture.v1";
    robot.name = "One-axis <fixture>";
    robot.mechanismId = "10000000-0000-4000-8000-000000000001";
    robot.axisId = "10000000-0000-4000-8000-000000000004";
    robot.baseFrameInWorld = {{1.0, 2.0, 3.0}, {std::sqrt(0.5), 0.0, 0.0, std::sqrt(0.5)},
                              fixture("fixture.base-frame", "Synthetic world pose for a 90 degree Z rotation")};
    robot.actuator = {"actuator.force.fixture.v1", "Ideal force actuator", robot.axisId, "N",
                      scalar(5.0, force(), "fixture.actuator.max-force", "Explicit 5 N limit for the synthetic benchmark only"),
                      std::nullopt};
    datumloop::SensorDefinition encoder;
    encoder.id = "sensor.encoder.fixture.v1";
    encoder.name = "Ideal linear encoder";
    encoder.type = datumloop::SensorType::Encoder;
    encoder.updatePeriodS = 0.02;
    encoder.latencyS = 0.01;
    encoder.evidence = fixture("fixture.encoder.spec", "Synthetic encoder timing fixture");
    encoder.countsPerMeter = scalar(10000.0, {{{0, -1, 0, 0, 0, 0, 0}}, "count/m"},
                                    "fixture.encoder.resolution", "Synthetic 10000 count/m resolution; not a hardware specification");
    robot.sensors.push_back(encoder);
    datumloop::SensorDefinition camera;
    camera.id = "sensor.camera.placeholder.v1";
    camera.name = "Camera placeholder";
    camera.type = datumloop::SensorType::CameraPlaceholder;
    camera.updatePeriodS = 0.02;
    camera.latencyS = 0.02;
    camera.evidence = fixture("fixture.camera.spec", "Synthetic placeholder dimensions and timing only");
    camera.imageWidthPixels = 640;
    camera.imageHeightPixels = 480;
    camera.horizontalFieldOfViewRad = 1.0471975511965976;
    robot.sensors.push_back(camera);
    return robot;
}

datumloop::RLEnvironmentSpec makeEnvironment(const datumloop::RobotModel& robot)
{
    datumloop::RLEnvironmentSpec spec;
    spec.id = "rl.mass-position.fixture.v1";
    spec.robotModelId = robot.id;
    spec.timeStep = scalar(0.01, datumloop::UnitDimension::second(), "fixture.step", "Synthetic fixed timestep for the educational environment");
    spec.episodeStepLimit = 100;
    spec.movingMass = scalar(1.0, datumloop::UnitDimension::kilogram(), "fixture.mass", "Synthetic 1 kg point-mass benchmark; not a measured machine property");
    spec.movingBodyDiagonalInertia = {{0.001, 0.0015, 0.002}, {{{1, 2, 0, 0, 0, 0, 0}}, "kg*m^2"},
                                      fixture("fixture.inertia", "Synthetic principal inertias for MJCF; not inferred from CAD")};
    spec.initialPosition = scalar(0.0, length(), "fixture.x0", "Synthetic initial condition");
    spec.initialVelocity = scalar(0.0, {{{0, 1, -1, 0, 0, 0, 0}}, "m/s"}, "fixture.v0", "Synthetic initial condition");
    spec.jointLowerPosition = scalar(-0.2, length(), "fixture.joint.lower", "Synthetic linear travel limit for the MJCF runtime check");
    spec.jointUpperPosition = scalar(0.2, length(), "fixture.joint.upper", "Synthetic linear travel limit for the MJCF runtime check");
    spec.targetPosition = scalar(0.1, length(), "fixture.goal", "Synthetic RL task target");
    spec.targetTolerance = scalar(0.001, length(), "fixture.tolerance", "Synthetic RL termination tolerance");
    spec.randomSeed = 42;
    spec.action = {robot.actuator.id,
                   scalar(-5.0, force(), "fixture.action.min", "Synthetic action bound derived from the fixture actuator limit"),
                   scalar(5.0, force(), "fixture.action.max", "Synthetic action bound derived from the fixture actuator limit")};
    spec.observation.sensorIds = {"sensor.encoder.fixture.v1", "sensor.camera.placeholder.v1"};
    return spec;
}

}  // namespace

int main()
{
    try {
        const auto engineering = makeEngineering();
        const auto robot = makeRobot();
        auto spec = makeEnvironment(robot);
        spec.validate(robot, engineering);

        datumloop::DeterministicMassEnvironment environment(robot, engineering, spec);
        const auto initial = environment.reset();
        require(initial.sensors.size() == 2 && !initial.sensors[0].valid &&
                    !initial.sensors[1].frameAvailable && initial.sensors[1].imageWidthPixels == 640,
                "reset must respect initial sensor latency and expose a camera descriptor without pixels");
        auto transition = environment.step({robot.actuator.id, 1.0, "N"});
        require(std::abs(transition.observation.timeS - 0.01) < 1e-14 &&
                    transition.observation.sensors[0].valid && transition.observation.sensors[0].value == 0.0 &&
                    transition.observation.sensors[0].captureTimeS == 0.0 && !transition.terminated,
                "RL environment step must preserve the action/observation contract");
        transition = environment.step({robot.actuator.id, 1.0, "N"});
        require(std::abs(transition.observation.timeS - 0.02) < 1e-14 &&
                    transition.observation.sensors[0].value == 0.0 && !transition.observation.sensors[1].valid &&
                    transition.observation.sensors[1].captureTimeS == 0.0 &&
                    transition.observation.sensors[1].deliveryTimeS == 0.02,
                "encoder and camera observations must respect their declared update and latency periods");
        transition = environment.step({robot.actuator.id, 1.0, "N"});
        require(std::abs(transition.observation.timeS - 0.03) < 1e-14 &&
                    std::abs(*transition.observation.sensors[0].value - 0.0002) < 1e-14,
                "delayed encoder samples must be quantized and delivered at their declared latency");
        require(!transition.observation.sensors[1].frameAvailable,
                "camera placeholder must never claim that a frame exists");
        requireThrows([&]() { (void)environment.step({robot.actuator.id, 6.0, "N"}); },
                      "environment accepted an action outside its declared limits");

        auto unsupportedTiming = spec;
        unsupportedTiming.timeStep.value = 0.015;
        requireThrows([&]() { unsupportedTiming.validate(robot, engineering); },
                      "environment accepted sensor timing that is not aligned to its time step");
        auto unsupportedFrame = robot;
        unsupportedFrame.baseFrameInWorld.orientationWorldWxyz[0] = 2.0;
        requireThrows([&]() { unsupportedFrame.validateAgainst(engineering); },
                      "robot model accepted a non-normalized coordinate-frame quaternion");
        auto unsupportedNoise = robot;
        unsupportedNoise.sensors.front().noise.distribution = datumloop::NoiseDistribution::Gaussian;
        requireThrows([&]() { unsupportedNoise.sensors.front().validate(); },
                      "Gaussian noise accepted without an evidence-backed standard deviation");
        auto evidenceBackedNoise = robot;
        evidenceBackedNoise.sensors.front().noise.distribution = datumloop::NoiseDistribution::Gaussian;
        evidenceBackedNoise.sensors.front().noise.standardDeviation = scalar(
            1e-5, length(), "fixture.encoder.noise", "Synthetic seeded-noise check only; not a hardware noise estimate");
        evidenceBackedNoise.validateAgainst(engineering);
        datumloop::DeterministicMassEnvironment noisyEnvironment(
            evidenceBackedNoise, engineering, makeEnvironment(evidenceBackedNoise));
        (void)noisyEnvironment.reset();
        const auto firstNoisySample = noisyEnvironment.step({evidenceBackedNoise.actuator.id, 0.0, "N"})
                                          .observation.sensors.front().value;
        (void)noisyEnvironment.reset();
        const auto repeatedNoisySample = noisyEnvironment.step({evidenceBackedNoise.actuator.id, 0.0, "N"})
                                             .observation.sensors.front().value;
        require(firstNoisySample && repeatedNoisySample && *firstNoisySample == *repeatedNoisySample,
                "evidence-backed sensor noise must repeat under the declared seed");
        auto unsupportedRandomization = spec;
        unsupportedRandomization.randomization.push_back({"friction", datumloop::UnitDimension::dimensionless(),
            datumloop::UncertaintyDistribution::Uniform, {0.2, 0.4}, {datumloop::EvidenceKind::SyntheticFixture, "", ""}, std::nullopt});
        requireThrows([&]() { unsupportedRandomization.validate(robot, engineering); },
                      "domain randomization accepted without evidence provenance");

        datumloop::SimulatedRobotIo simulated(
            {robot.axisId, 0.0, 0.25, 0.0, "m", "m/s"}, robot.actuator.id, "N", -5.0, 5.0);
        const datumloop::ControllerFunction controller = [&robot](const datumloop::ControlState& state) {
            return datumloop::ActuatorCommand{robot.actuator.id, state.position < 1.0 ? 2.0 : 0.0, "N"};
        };
        datumloop::runControllerCycle(simulated, simulated, controller);
        require(simulated.lastCommand() && simulated.lastCommand()->value == 2.0,
                "shared controller cycle did not connect to simulated state and command interfaces");
        datumloop::DisabledHardwareCommandInterface hardware;
        requireThrows([&]() { datumloop::runControllerCycle(simulated, hardware, controller); },
                      "hardware command interface allowed an actuator write");

        const std::string mjcf = datumloop::exportMuJoCoMjcf(robot, engineering, spec);
        QXmlStreamReader mjcfReader(QString::fromStdString(mjcf));
        while (!mjcfReader.atEnd()) (void)mjcfReader.readNext();
        require(mjcf.find("<mujoco model=\"One-axis &lt;fixture&gt;\">") != std::string::npos,
                "MuJoCo export did not escape the model name");
        require(mjcf.find("pos=\"1 2.1") != std::string::npos &&
                    mjcf.find("quat=\"0.70710678118654757 0 0 0.70710678118654757\"") != std::string::npos,
                "MuJoCo export did not transform the DatumLoop base frame and joint origin");
        require(mjcf.find("type=\"slide\"") != std::string::npos && mjcf.find("<motor") != std::string::npos &&
                    mjcf.find("ctrlrange=\"-5 5\"") != std::string::npos &&
                    mjcf.find("limited=\"true\" range=\"-0.2") != std::string::npos,
                "MuJoCo export did not emit the linear actuator, action bounds, and sourced joint limits");
        require(mjcf.find("<jointpos") != std::string::npos && mjcf.find("<jointvel") != std::string::npos &&
                    mjcf.find("sensor.camera.placeholder") == std::string::npos,
                "MuJoCo export must expose encoder state and omit camera-placeholder data");
        require(!mjcfReader.hasError(), "MuJoCo export did not produce well-formed XML");

        if (const char* outputPath = std::getenv("DATUMLOOP_MJCF_OUTPUT")) {
            const std::filesystem::path path(outputPath);
            std::filesystem::create_directories(path.parent_path());
            std::ofstream output(path, std::ios::binary);
            if (!output) throw std::runtime_error("could not write exported MJCF to the configured runtime-test path");
            output << mjcf;
            if (!output) throw std::runtime_error("could not finish writing the exported MJCF runtime-test fixture");
        }

        std::cout << "DatumLoop robot model, evidence-gated sensors/randomization, RL I/O, MuJoCo export, and disabled hardware boundary passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "Robotics environment test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
