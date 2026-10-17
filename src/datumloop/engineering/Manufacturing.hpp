#pragma once
#include "datumloop/engineering/MotorCarrierDesign.hpp"
namespace datumloop {
enum class ManufacturingProcess { ThreeAxisCnc, Drilling, Reaming, Threading };
enum class ManufacturingCheckStatus { Passed, Failed, NotApplicable, Unknown };
struct ManufacturingFeature {
    std::string featureId;
    ManufacturingProcess process = ManufacturingProcess::ThreeAxisCnc;
    Vector3 toolApproachUnit{0,0,1};
    std::string description;
};
struct ManufacturingConstraint {
    std::string id;
    std::vector<std::string> requirementIds;
    std::string method;
};
struct ManufacturingCheck {
    ManufacturingConstraint constraint;
    ManufacturingCheckStatus status = ManufacturingCheckStatus::Unknown;
    std::optional<double> observed;
    std::optional<double> limit;
    std::string unit;
    std::string reason;
};
struct ManufacturingEvaluation {
    std::string cadObjectId;
    std::uint64_t documentRevision = 0;
    std::vector<ManufacturingFeature> features;
    std::vector<ManufacturingCheck> checks;
    std::vector<std::string> limitations;
    [[nodiscard]] bool geometricScreeningPassed() const noexcept;
};
// Qualified only for this exact plate/normal-through-bore feature recipe.
// Unsupported features produce UNKNOWN, never a generic CNC-success claim.
ManufacturingEvaluation evaluateMotorCarrierManufacturing(const MotorCarrierCandidate& candidate);
std::string manufacturingCheckStatusName(ManufacturingCheckStatus status);
std::string manufacturingProcessName(ManufacturingProcess process);
} // namespace datumloop
