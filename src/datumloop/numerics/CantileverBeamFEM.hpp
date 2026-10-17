#pragma once

#include <vector>
#include <string>

namespace datumloop {

// One-dimensional Timoshenko beam benchmark with a fixed-free boundary.
// This is a verification example, not a general-purpose structural solver.
struct CantileverBeamModel {
    double lengthM = 0.0;
    double youngModulusPa = 0.0;
    double secondMomentOfAreaM4 = 0.0;
    double shearStiffnessN = 0.0;
    double distributedLoadNPerM = 0.0;

    void validate() const;
};

struct CantileverBeamResult {
    int elementCount = 0;
    double tipDisplacementM = 0.0;
    double analyticalTipDisplacementM = 0.0;
    double relativeError = 0.0;
    double strainEnergyJ = 0.0;
};

[[nodiscard]] CantileverBeamResult solveCantileverBeam(const CantileverBeamModel& model,
                                                       int elementCount);

struct ReducedModelValidationSample {
    double inputLoadNPerM = 0.0;
    double reducedPredictionM = 0.0;
    double highFidelityResponseM = 0.0;
    double relativeError = 0.0;
};

class IReducedOrderModel {
public:
    virtual ~IReducedOrderModel() = default;
    [[nodiscard]] virtual std::string modelId() const = 0;
    [[nodiscard]] virtual std::string sourceSolverId() const = 0;
    [[nodiscard]] virtual double predictTipDisplacement(double distributedLoadNPerM) const = 0;
    [[nodiscard]] virtual double maxRelativeValidationError() const noexcept = 0;
};

struct CantileverReducedModel final : public IReducedOrderModel {
    std::string id = "cantilever.linear-compliance-rom.v1";
    std::string highFidelitySolverId;
    std::string highFidelitySolverVersion;
    std::string sourceModelRevision;
    double sourceLengthM = 0.0;
    double sourceYoungModulusPa = 0.0;
    double sourceSecondMomentOfAreaM4 = 0.0;
    double sourceShearStiffnessN = 0.0;
    double complianceMPerNPerM = 0.0;
    int trainingElementCount = 0;
    int validationElementCount = 0;
    double minimumTrainingLoadNPerM = 0.0;
    double maximumTrainingLoadNPerM = 0.0;
    double maximumRelativeValidationError = 0.0;
    double rmsRelativeValidationError = 0.0;
    std::vector<double> trainingLoadsNPerM;
    std::vector<double> validationRelativeErrors;
    std::vector<ReducedModelValidationSample> heldOutSamples;

    [[nodiscard]] std::string modelId() const override { return id; }
    [[nodiscard]] std::string sourceSolverId() const override { return highFidelitySolverId; }
    [[nodiscard]] double predictTipDisplacement(double distributedLoadNPerM) const override;
    [[nodiscard]] double maxRelativeValidationError() const noexcept override
    {
        return maximumRelativeValidationError;
    }
};

[[nodiscard]] CantileverReducedModel fitCantileverReducedModel(
    const CantileverBeamModel& model,
    int trainingElementCount,
    int validationElementCount,
    const std::vector<double>& trainingLoadsNPerM,
    const std::vector<double>& validationLoadsNPerM);

}  // namespace datumloop
