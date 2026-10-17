#include "datumloop/numerics/FemSolver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace datumloop {
namespace {

constexpr const char* kCantileverEquationId = "structural.timoshenko-beam.isotropic.v1";
constexpr const char* kCantileverBenchmarkId = "timoshenko-cantilever-uniform-load-v1";

}  // namespace

void FemSolverResult::validate() const
{
    if (solverId.empty() || solverVersion.empty() || elementCount <= 0 || !converged ||
        !std::isfinite(tipDisplacementM) || !std::isfinite(analyticalTipDisplacementM) ||
        !std::isfinite(relativeError) || relativeError < 0.0 || !std::isfinite(strainEnergyJ) ||
        strainEnergyJ < 0.0 || !std::isfinite(relativeResidual) || relativeResidual < 0.0 ||
        !std::isfinite(executionTimeSeconds) || executionTimeSeconds < 0.0) {
        throw std::invalid_argument("finite-element result requires finite values and a converged solve");
    }
    if (provenance.equationDefinitionId != kCantileverEquationId ||
        provenance.equationDefinitionRevision.empty() ||
        provenance.equationMaturity != ModelMaturity::E4 || provenance.backendId != solverId ||
        provenance.backendVersion != solverVersion || provenance.meshDescription.empty() ||
        provenance.elementFamily.empty() || provenance.polynomialOrder <= 0 ||
        !std::isfinite(provenance.absoluteTolerance) || provenance.absoluteTolerance <= 0.0 ||
        !std::isfinite(provenance.relativeTolerance) || provenance.relativeTolerance <= 0.0 ||
        !provenance.converged || provenance.benchmarkId != kCantileverBenchmarkId ||
        provenance.benchmarkStatus.empty() || provenance.inputRevision.empty()) {
        throw std::invalid_argument("finite-element result is missing equation, discretization, or benchmark provenance");
    }
}

std::string DatumReferenceFemSolver::solverId() const
{
    return "datumloop.reference-timoshenko-fem";
}

std::string DatumReferenceFemSolver::solverVersion() const
{
    return "1.0";
}

FemSolverResult DatumReferenceFemSolver::solveCantilever(const CantileverBeamModel& model,
                                                         int elementCount) const
{
    const CantileverBeamResult reference = solveCantileverBeam(model, elementCount);
    FemSolverResult result;
    result.solverId = solverId();
    result.solverVersion = solverVersion();
    result.elementCount = elementCount;
    result.tipDisplacementM = reference.tipDisplacementM;
    result.analyticalTipDisplacementM = reference.analyticalTipDisplacementM;
    result.relativeError = reference.relativeError;
    result.strainEnergyJ = reference.strainEnergyJ;
    result.relativeResidual = 0.0;
    result.executionTimeSeconds = 0.0;
    result.converged = true;
    result.provenance = {kCantileverEquationId, "1", ModelMaturity::E4, "", "",
                         result.solverId, result.solverVersion,
                         std::to_string(elementCount) + " uniform line elements",
                         "Timoshenko 1D reference element", 1, 1e-12, 1e-12, true,
                         kCantileverBenchmarkId, "analytical-comparison-recorded", "cantilever-model-v1"};
    result.validate();
    return result;
}

CantileverReducedModel fitCantileverReducedModel(const IFemSolver& highFidelitySolver,
                                                const CantileverBeamModel& model,
                                                int trainingElementCount,
                                                int validationElementCount,
                                                const std::vector<double>& trainingLoadsNPerM,
                                                const std::vector<double>& heldOutLoadsNPerM)
{
    model.validate();
    if (trainingElementCount <= 0 || validationElementCount <= 0 || trainingLoadsNPerM.empty() ||
        heldOutLoadsNPerM.empty()) {
        throw std::invalid_argument("solver-backed ROM fit requires meshes and training/held-out loads");
    }

    CantileverReducedModel result;
    result.highFidelitySolverId = highFidelitySolver.solverId();
    result.highFidelitySolverVersion = highFidelitySolver.solverVersion();
    if (result.highFidelitySolverId.empty() || result.highFidelitySolverVersion.empty()) {
        throw std::invalid_argument("high-fidelity solver must provide stable identity and version provenance");
    }
    result.sourceModelRevision = "cantilever-model-v1";
    result.sourceLengthM = model.lengthM;
    result.sourceYoungModulusPa = model.youngModulusPa;
    result.sourceSecondMomentOfAreaM4 = model.secondMomentOfAreaM4;
    result.sourceShearStiffnessN = model.shearStiffnessN;
    result.trainingElementCount = trainingElementCount;
    result.validationElementCount = validationElementCount;
    result.minimumTrainingLoadNPerM = std::numeric_limits<double>::infinity();
    double denominator = 0.0;
    double numerator = 0.0;
    for (const double load : trainingLoadsNPerM) {
        if (!std::isfinite(load) || load <= 0.0) {
            throw std::invalid_argument("solver-backed ROM training loads must be finite and positive");
        }
        CantileverBeamModel trainingModel = model;
        trainingModel.distributedLoadNPerM = load;
        const FemSolverResult response = highFidelitySolver.solveCantilever(trainingModel, trainingElementCount);
        response.validate();
        if (response.solverId != result.highFidelitySolverId ||
            response.solverVersion != result.highFidelitySolverVersion) {
            throw std::invalid_argument("high-fidelity solver identity changed during ROM training");
        }
        numerator += load * response.tipDisplacementM;
        denominator += load * load;
        result.trainingLoadsNPerM.push_back(load);
        result.minimumTrainingLoadNPerM = std::min(result.minimumTrainingLoadNPerM, load);
        result.maximumTrainingLoadNPerM = std::max(result.maximumTrainingLoadNPerM, load);
    }
    if (denominator <= 0.0) throw std::invalid_argument("solver-backed ROM training loads have zero energy");
    result.complianceMPerNPerM = numerator / denominator;

    double squaredRelativeErrorSum = 0.0;
    for (const double load : heldOutLoadsNPerM) {
        if (!std::isfinite(load) || load < result.minimumTrainingLoadNPerM ||
            load > result.maximumTrainingLoadNPerM) {
            throw std::invalid_argument("ROM held-out loads must lie inside the training-load domain");
        }
        CantileverBeamModel validationModel = model;
        validationModel.distributedLoadNPerM = load;
        const FemSolverResult highFidelity = highFidelitySolver.solveCantilever(validationModel, validationElementCount);
        highFidelity.validate();
        const double prediction = result.predictTipDisplacement(load);
        const double relativeError = std::abs(prediction - highFidelity.tipDisplacementM) /
                                     std::abs(highFidelity.tipDisplacementM);
        result.validationRelativeErrors.push_back(relativeError);
        result.maximumRelativeValidationError = std::max(result.maximumRelativeValidationError, relativeError);
        squaredRelativeErrorSum += relativeError * relativeError;
        result.heldOutSamples.push_back({load, prediction, highFidelity.tipDisplacementM, relativeError});
    }
    result.rmsRelativeValidationError = std::sqrt(squaredRelativeErrorSum /
                                                   static_cast<double>(heldOutLoadsNPerM.size()));
    return result;
}

}  // namespace datumloop
