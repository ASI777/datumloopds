#pragma once

#include <vector>

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
};

[[nodiscard]] CantileverBeamResult solveCantileverBeam(const CantileverBeamModel& model,
                                                       int elementCount);

struct CantileverReducedModel {
    double complianceMPerNPerM = 0.0;
    int trainingElementCount = 0;
    int validationElementCount = 0;
    double maximumRelativeValidationError = 0.0;
    std::vector<double> validationRelativeErrors;

    [[nodiscard]] double predictTipDisplacement(double distributedLoadNPerM) const;
};

[[nodiscard]] CantileverReducedModel fitCantileverReducedModel(
    const CantileverBeamModel& model,
    int trainingElementCount,
    int validationElementCount,
    const std::vector<double>& trainingLoadsNPerM,
    const std::vector<double>& validationLoadsNPerM);

}  // namespace datumloop
