#pragma once

#include "datumloop/engineering/StructuralAnalysis.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace datumloop {

struct StructuralRomInputDomain {
    double minimumAppliedForceN = 0.0;
    double maximumAppliedForceN = 0.0;
    double minimumYoungsModulusPa = 0.0;
    double maximumYoungsModulusPa = 0.0;

    [[nodiscard]] bool contains(double appliedForceN, double youngsModulusPa) const noexcept;
    void validate() const;
};

struct StructuralRomHighFidelitySample {
    double appliedForceMagnitudeN = 0.0;
    double youngsModulusPa = 0.0;
    StructuralAnalysisRun result;
};

struct StructuralRomSampleRecord {
    double appliedForceMagnitudeN = 0.0;
    double youngsModulusPa = 0.0;
    double highFidelityTipDisplacementM = 0.0;
    double romTipDisplacementM = 0.0;
    double effectiveStiffnessNPerM = 0.0;
    double relativeError = 0.0;
    std::string solverRunId;
    std::string materialRevision;
};

struct StructuralResponseComparisonSample {
    double appliedForceMagnitudeN = 0.0;
    double youngsModulusPa = 0.0;
    double referenceTipDeflectionM = 0.0;
    double predictedTipDeflectionM = 0.0;
    std::string referenceRunId;
};

// The error fields are absolute relative errors normalized by the reference
// tip-deflection magnitude. Ties for the maximum retain the first sample.
struct StructuralResponseErrorMetrics {
    std::size_t sampleCount = 0;
    double meanAbsoluteRelativeError = 0.0;
    double rmsRelativeError = 0.0;
    double maximumRelativeError = 0.0;
    StructuralResponseComparisonSample worstOperatingPoint;
};

// Requires positive finite inputs/responses and a nonempty reference run ID.
[[nodiscard]] double relativeTipDeflectionError(
    const StructuralResponseComparisonSample& comparison);
[[nodiscard]] StructuralResponseErrorMetrics compareStructuralTipDeflections(
    const std::vector<StructuralResponseComparisonSample>& comparisons);

// A bounded response surface fitted from MFEM static linear-elastic runs.
// Its normalized compliance has units 1/m and follows
// u_tip = normalizedCompliancePerM * force / Young's modulus.
// This is a first response ROM, not a modal or general-purpose reduced solver.
struct StructuralElasticityRom {
    std::string id;
    std::string sourceProjectId;
    std::string cadObjectId;
    std::string materialDefinitionId;
    std::string equationDefinitionId;
    std::string solverId;
    std::string solverVersion;
    AnalysisMeshProvenance mesh;
    std::vector<std::string> topologyReferenceIds;
    std::vector<std::string> boundaryConditionIds;
    Vector3 appliedForceDirectionUnit{0.0, 0.0, 0.0};
    StructuralRomInputDomain inputDomain;
    double normalizedCompliancePerM = 0.0;
    double validationToleranceRelative = 0.01;
    double maximumRelativeValidationError = 0.0;
    double rmsRelativeValidationError = 0.0;
    bool validationPassed = false;
    bool rejectsOutOfDomainInputs = true;
    std::vector<StructuralRomSampleRecord> trainingSamples;
    std::vector<StructuralRomSampleRecord> validationSamples;

    [[nodiscard]] double predictTipDisplacement(double appliedForceN,
                                                double youngsModulusPa) const;
    [[nodiscard]] double predictEffectiveStiffness(double appliedForceN,
                                                   double youngsModulusPa) const;
    void validate() const;
};

[[nodiscard]] StructuralElasticityRom fitStructuralElasticityRom(
    std::string modelId,
    const std::vector<StructuralRomHighFidelitySample>& trainingSamples,
    const std::vector<StructuralRomHighFidelitySample>& heldOutSamples,
    double validationToleranceRelative = 0.01);

}  // namespace datumloop
