#include "datumloop/numerics/StructuralReducedModel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace datumloop {
namespace {

bool finite(double value) { return std::isfinite(value); }

void requireText(const std::string& value, const char* message)
{
    if (value.empty()) throw std::invalid_argument(message);
}

double norm(const Vector3& value)
{
    return std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
}

double relativeError(double prediction, double reference)
{
    return std::abs(prediction - reference) / std::abs(reference);
}

void validateInput(double appliedForceN, double youngsModulusPa)
{
    if (!finite(appliedForceN) || appliedForceN <= 0.0 ||
        !finite(youngsModulusPa) || youngsModulusPa <= 0.0) {
        throw std::invalid_argument("structural ROM inputs must be finite positive force and Young's modulus values");
    }
}

void checkSampleCompatibility(const StructuralRomHighFidelitySample& sample,
                              const StructuralAnalysisRun& reference)
{
    validateInput(sample.appliedForceMagnitudeN, sample.youngsModulusPa);
    sample.result.validate(reference.provenance.boundaryConditionIds);
    if (!sample.result.converged || !sample.result.meshSnapshot.has_value() ||
        sample.result.provenance.solverRunId.empty() || sample.result.provenance.materialRevision.empty()) {
        throw std::invalid_argument("structural ROM samples require converged MFEM runs with mesh and material provenance");
    }
    if (sample.result.provenance.sourceProjectId != reference.provenance.sourceProjectId ||
        sample.result.provenance.cadObjectId != reference.provenance.cadObjectId ||
        sample.result.provenance.geometryFeatureId != reference.provenance.geometryFeatureId ||
        sample.result.provenance.materialDefinitionId != reference.provenance.materialDefinitionId ||
        sample.result.provenance.materialStateId != reference.provenance.materialStateId ||
        sample.result.provenance.analysisTemperatureK != reference.provenance.analysisTemperatureK ||
        sample.result.provenance.temperatureRangeVerified != reference.provenance.temperatureRangeVerified ||
        sample.result.provenance.equationDefinitionId != reference.provenance.equationDefinitionId ||
        sample.result.provenance.equationMaturity != reference.provenance.equationMaturity ||
        sample.result.provenance.solverId != reference.provenance.solverId ||
        sample.result.provenance.solverVersion != reference.provenance.solverVersion ||
        sample.result.provenance.absoluteTolerance != reference.provenance.absoluteTolerance ||
        sample.result.provenance.relativeTolerance != reference.provenance.relativeTolerance ||
        sample.result.provenance.maximumIterations != reference.provenance.maximumIterations ||
        sample.result.provenance.mesh != reference.provenance.mesh ||
        sample.result.provenance.topologyReferenceIds != reference.provenance.topologyReferenceIds ||
        sample.result.provenance.boundaryConditionIds != reference.provenance.boundaryConditionIds) {
        throw std::invalid_argument("structural ROM samples must share CAD, material identity, equation, solver, and mesh provenance");
    }
    const double appliedForce = norm(sample.result.appliedForceN);
    const double forceTolerance = 1e-9 * std::max(1.0, sample.appliedForceMagnitudeN);
    if (!finite(appliedForce) || std::abs(appliedForce - sample.appliedForceMagnitudeN) > forceTolerance ||
        !finite(sample.result.tipDisplacementM) || sample.result.tipDisplacementM <= 0.0) {
        throw std::invalid_argument("structural ROM sample inputs do not match a finite nonzero solver response");
    }
    const double referenceForce = norm(reference.appliedForceN);
    if (!finite(referenceForce) || referenceForce <= 0.0) {
        throw std::invalid_argument("structural ROM reference run has no nonzero applied-force direction");
    }
    for (std::size_t axis = 0; axis < sample.result.appliedForceN.size(); ++axis) {
        const double sampleDirection = sample.result.appliedForceN[axis] / appliedForce;
        const double referenceDirection = reference.appliedForceN[axis] / referenceForce;
        if (std::abs(sampleDirection - referenceDirection) > 1e-9) {
            throw std::invalid_argument("structural ROM samples must preserve the reference load direction");
        }
    }
}

StructuralRomSampleRecord makeRecord(const StructuralRomHighFidelitySample& sample,
                                     double normalizedCompliancePerM)
{
    const double prediction = normalizedCompliancePerM * sample.appliedForceMagnitudeN / sample.youngsModulusPa;
    return {sample.appliedForceMagnitudeN,
            sample.youngsModulusPa,
            sample.result.tipDisplacementM,
            prediction,
            sample.appliedForceMagnitudeN / prediction,
            relativeError(prediction, sample.result.tipDisplacementM),
            sample.result.provenance.solverRunId,
            sample.result.provenance.materialRevision};
}

}  // namespace

double relativeTipDeflectionError(const StructuralResponseComparisonSample& comparison)
{
    if (!finite(comparison.appliedForceMagnitudeN) || comparison.appliedForceMagnitudeN <= 0.0 ||
        !finite(comparison.youngsModulusPa) || comparison.youngsModulusPa <= 0.0 ||
        !finite(comparison.referenceTipDeflectionM) || comparison.referenceTipDeflectionM <= 0.0 ||
        !finite(comparison.predictedTipDeflectionM) || comparison.predictedTipDeflectionM <= 0.0 ||
        comparison.referenceRunId.empty()) {
        throw std::invalid_argument("structural response comparison requires finite positive values and reference provenance");
    }
    const double error = std::abs(comparison.predictedTipDeflectionM - comparison.referenceTipDeflectionM) /
                         comparison.referenceTipDeflectionM;
    if (!finite(error)) {
        throw std::overflow_error("structural response relative error exceeds the supported numeric range");
    }
    return error;
}

StructuralResponseErrorMetrics compareStructuralTipDeflections(
    const std::vector<StructuralResponseComparisonSample>& comparisons)
{
    if (comparisons.empty()) {
        throw std::invalid_argument("structural response comparison requires at least one sample");
    }
    StructuralResponseErrorMetrics metrics;
    metrics.sampleCount = comparisons.size();
    metrics.worstOperatingPoint = comparisons.front();
    long double absoluteErrorSum = 0.0L;
    long double squaredErrorSum = 0.0L;
    for (const StructuralResponseComparisonSample& comparison : comparisons) {
        const double error = relativeTipDeflectionError(comparison);
        const long double extendedError = static_cast<long double>(error);
        absoluteErrorSum += extendedError;
        squaredErrorSum += extendedError * extendedError;
        if (error > metrics.maximumRelativeError) {
            metrics.maximumRelativeError = error;
            metrics.worstOperatingPoint = comparison;
        }
    }
    const long double sampleCount = static_cast<long double>(metrics.sampleCount);
    metrics.meanAbsoluteRelativeError = static_cast<double>(absoluteErrorSum / sampleCount);
    metrics.rmsRelativeError = static_cast<double>(std::sqrt(squaredErrorSum / sampleCount));
    if (!finite(metrics.meanAbsoluteRelativeError) || !finite(metrics.rmsRelativeError)) {
        throw std::overflow_error("structural response error metrics exceed the supported numeric range");
    }
    return metrics;
}

bool StructuralRomInputDomain::contains(double appliedForceN, double youngsModulusPa) const noexcept
{
    return finite(appliedForceN) && finite(youngsModulusPa) &&
           appliedForceN >= minimumAppliedForceN && appliedForceN <= maximumAppliedForceN &&
           youngsModulusPa >= minimumYoungsModulusPa && youngsModulusPa <= maximumYoungsModulusPa;
}

void StructuralRomInputDomain::validate() const
{
    if (!finite(minimumAppliedForceN) || !finite(maximumAppliedForceN) ||
        !finite(minimumYoungsModulusPa) || !finite(maximumYoungsModulusPa) ||
        minimumAppliedForceN <= 0.0 || maximumAppliedForceN < minimumAppliedForceN ||
        minimumYoungsModulusPa <= 0.0 || maximumYoungsModulusPa < minimumYoungsModulusPa) {
        throw std::invalid_argument("structural ROM input domain requires finite, positive ordered bounds");
    }
}

double StructuralElasticityRom::predictTipDisplacement(double appliedForceN,
                                                       double youngsModulusPa) const
{
    validateInput(appliedForceN, youngsModulusPa);
    inputDomain.validate();
    if (!inputDomain.contains(appliedForceN, youngsModulusPa)) {
        throw std::out_of_range("structural ROM prediction is outside its validated input domain");
    }
    if (!finite(normalizedCompliancePerM) || normalizedCompliancePerM <= 0.0) {
        throw std::logic_error("structural ROM does not contain a positive finite normalized compliance coefficient");
    }
    return normalizedCompliancePerM * appliedForceN / youngsModulusPa;
}

double StructuralElasticityRom::predictEffectiveStiffness(double appliedForceN,
                                                          double youngsModulusPa) const
{
    return appliedForceN / predictTipDisplacement(appliedForceN, youngsModulusPa);
}

void StructuralElasticityRom::validate() const
{
    requireText(id, "structural ROM requires a stable ID");
    requireText(sourceProjectId, "structural ROM requires source project provenance");
    requireText(cadObjectId, "structural ROM requires source CAD provenance");
    requireText(materialDefinitionId, "structural ROM requires source material provenance");
    requireText(equationDefinitionId, "structural ROM requires equation provenance");
    requireText(solverId, "structural ROM requires solver provenance");
    requireText(solverVersion, "structural ROM requires solver version provenance");
    inputDomain.validate();
    if (!finite(normalizedCompliancePerM) || normalizedCompliancePerM <= 0.0 ||
        inputDomain.minimumAppliedForceN == inputDomain.maximumAppliedForceN ||
        inputDomain.minimumYoungsModulusPa == inputDomain.maximumYoungsModulusPa ||
        !finite(validationToleranceRelative) || validationToleranceRelative <= 0.0 ||
        !finite(maximumRelativeValidationError) || maximumRelativeValidationError < 0.0 ||
        !finite(rmsRelativeValidationError) || rmsRelativeValidationError < 0.0 ||
        trainingSamples.empty() || validationSamples.empty() || !rejectsOutOfDomainInputs) {
        throw std::invalid_argument("structural ROM requires bounded inputs, samples, finite metrics, and fail-closed extrapolation");
    }
    for (const std::string& idValue : topologyReferenceIds) {
        requireText(idValue, "structural ROM topology provenance IDs must not be empty");
    }
    for (const std::string& idValue : boundaryConditionIds) {
        requireText(idValue, "structural ROM boundary-condition provenance IDs must not be empty");
    }
    const double loadDirectionNorm = norm(appliedForceDirectionUnit);
    if (!finite(loadDirectionNorm) || std::abs(loadDirectionNorm - 1.0) > 1e-9) {
        throw std::invalid_argument("structural ROM must retain its unit applied-force direction");
    }
    std::unordered_set<std::string> allRunIds;
    std::set<std::pair<double, double>> allInputPairs;
    const auto validateRecords = [this, &allRunIds, &allInputPairs](
                                     const std::vector<StructuralRomSampleRecord>& samples) {
        double maximum = 0.0;
        double squaredErrorSum = 0.0;
        for (const StructuralRomSampleRecord& sample : samples) {
            validateInput(sample.appliedForceMagnitudeN, sample.youngsModulusPa);
            if (!inputDomain.contains(sample.appliedForceMagnitudeN, sample.youngsModulusPa) ||
                !finite(sample.highFidelityTipDisplacementM) || sample.highFidelityTipDisplacementM <= 0.0 ||
                !finite(sample.romTipDisplacementM) || sample.romTipDisplacementM <= 0.0 ||
                !finite(sample.effectiveStiffnessNPerM) || sample.effectiveStiffnessNPerM <= 0.0 ||
                !finite(sample.relativeError) || sample.relativeError < 0.0 ||
                sample.solverRunId.empty() || sample.materialRevision.empty() ||
                !allRunIds.insert(sample.solverRunId).second ||
                !allInputPairs.emplace(sample.appliedForceMagnitudeN, sample.youngsModulusPa).second) {
                throw std::invalid_argument("structural ROM sample record is incomplete or outside the input domain");
            }
            const double expectedPrediction = normalizedCompliancePerM * sample.appliedForceMagnitudeN /
                                              sample.youngsModulusPa;
            const double expectedError = relativeError(expectedPrediction, sample.highFidelityTipDisplacementM);
            const double tolerance = 1e-10 * std::max(1.0, std::abs(expectedPrediction));
            if (std::abs(sample.romTipDisplacementM - expectedPrediction) > tolerance ||
                std::abs(sample.relativeError - expectedError) > 1e-10 ||
                std::abs(sample.effectiveStiffnessNPerM -
                         sample.appliedForceMagnitudeN / sample.romTipDisplacementM) > tolerance) {
                throw std::invalid_argument("structural ROM sample metrics do not match its response law");
            }
            maximum = std::max(maximum, expectedError);
            squaredErrorSum += expectedError * expectedError;
        }
        return std::pair{maximum, std::sqrt(squaredErrorSum / static_cast<double>(samples.size()))};
    };
    (void)validateRecords(trainingSamples);
    const auto [computedMaximum, computedRms] = validateRecords(validationSamples);
    const double metricTolerance = 1e-10 * std::max(1.0, computedMaximum);
    if (std::abs(maximumRelativeValidationError - computedMaximum) > metricTolerance ||
        std::abs(rmsRelativeValidationError - computedRms) > metricTolerance ||
        validationPassed != (computedMaximum <= validationToleranceRelative)) {
        throw std::invalid_argument("structural ROM validation metrics are inconsistent with held-out samples");
    }
}

StructuralElasticityRom fitStructuralElasticityRom(
    std::string modelId,
    const std::vector<StructuralRomHighFidelitySample>& trainingSamples,
    const std::vector<StructuralRomHighFidelitySample>& heldOutSamples,
    double validationToleranceRelative)
{
    requireText(modelId, "structural ROM fit requires a model ID");
    if (trainingSamples.size() < 2 || heldOutSamples.empty() ||
        !finite(validationToleranceRelative) || validationToleranceRelative <= 0.0) {
        throw std::invalid_argument("structural ROM fit requires multiple training samples, held-out samples, and a positive tolerance");
    }
    const StructuralAnalysisRun& reference = trainingSamples.front().result;
    StructuralElasticityRom rom;
    rom.id = std::move(modelId);
    rom.sourceProjectId = reference.provenance.sourceProjectId;
    rom.cadObjectId = reference.provenance.cadObjectId;
    rom.materialDefinitionId = reference.provenance.materialDefinitionId;
    rom.equationDefinitionId = reference.provenance.equationDefinitionId;
    rom.solverId = reference.provenance.solverId;
    rom.solverVersion = reference.provenance.solverVersion;
    rom.mesh = reference.provenance.mesh;
    rom.topologyReferenceIds = reference.provenance.topologyReferenceIds;
    rom.boundaryConditionIds = reference.provenance.boundaryConditionIds;
    const double referenceForce = norm(reference.appliedForceN);
    if (!finite(referenceForce) || referenceForce <= 0.0) {
        throw std::invalid_argument("structural ROM reference run has no nonzero applied-force direction");
    }
    for (std::size_t axis = 0; axis < rom.appliedForceDirectionUnit.size(); ++axis) {
        rom.appliedForceDirectionUnit[axis] = reference.appliedForceN[axis] / referenceForce;
    }
    rom.validationToleranceRelative = validationToleranceRelative;
    rom.inputDomain.minimumAppliedForceN = std::numeric_limits<double>::infinity();
    rom.inputDomain.maximumAppliedForceN = 0.0;
    rom.inputDomain.minimumYoungsModulusPa = std::numeric_limits<double>::infinity();
    rom.inputDomain.maximumYoungsModulusPa = 0.0;

    double numerator = 0.0;
    double denominator = 0.0;
    for (const StructuralRomHighFidelitySample& sample : trainingSamples) {
        checkSampleCompatibility(sample, reference);
        const double scaledLoad = sample.appliedForceMagnitudeN / sample.youngsModulusPa;
        numerator += scaledLoad * sample.result.tipDisplacementM;
        denominator += scaledLoad * scaledLoad;
        rom.inputDomain.minimumAppliedForceN = std::min(rom.inputDomain.minimumAppliedForceN,
                                                        sample.appliedForceMagnitudeN);
        rom.inputDomain.maximumAppliedForceN = std::max(rom.inputDomain.maximumAppliedForceN,
                                                        sample.appliedForceMagnitudeN);
        rom.inputDomain.minimumYoungsModulusPa = std::min(rom.inputDomain.minimumYoungsModulusPa,
                                                         sample.youngsModulusPa);
        rom.inputDomain.maximumYoungsModulusPa = std::max(rom.inputDomain.maximumYoungsModulusPa,
                                                         sample.youngsModulusPa);
    }
    if (!finite(denominator) || denominator <= 0.0 || !finite(numerator) || numerator <= 0.0) {
        throw std::invalid_argument("structural ROM training samples do not define a positive compliance response");
    }
    rom.inputDomain.validate();
    if (rom.inputDomain.minimumAppliedForceN == rom.inputDomain.maximumAppliedForceN ||
        rom.inputDomain.minimumYoungsModulusPa == rom.inputDomain.maximumYoungsModulusPa) {
        throw std::invalid_argument("structural ROM training must span both applied force and Young's modulus");
    }
    rom.normalizedCompliancePerM = numerator / denominator;
    for (const StructuralRomHighFidelitySample& sample : trainingSamples) {
        if (!rom.inputDomain.contains(sample.appliedForceMagnitudeN, sample.youngsModulusPa)) {
            throw std::logic_error("structural ROM training input was omitted from its fitted domain");
        }
        rom.trainingSamples.push_back(makeRecord(sample, rom.normalizedCompliancePerM));
    }

    double squaredErrorSum = 0.0;
    for (const StructuralRomHighFidelitySample& sample : heldOutSamples) {
        checkSampleCompatibility(sample, reference);
        if (!rom.inputDomain.contains(sample.appliedForceMagnitudeN, sample.youngsModulusPa)) {
            throw std::invalid_argument("structural ROM held-out sample lies outside the training input domain");
        }
        StructuralRomSampleRecord record = makeRecord(sample, rom.normalizedCompliancePerM);
        rom.maximumRelativeValidationError = std::max(rom.maximumRelativeValidationError, record.relativeError);
        squaredErrorSum += record.relativeError * record.relativeError;
        rom.validationSamples.push_back(std::move(record));
    }
    rom.rmsRelativeValidationError = std::sqrt(squaredErrorSum / static_cast<double>(heldOutSamples.size()));
    rom.validationPassed = rom.maximumRelativeValidationError <= rom.validationToleranceRelative;
    rom.validate();
    return rom;
}

}  // namespace datumloop
