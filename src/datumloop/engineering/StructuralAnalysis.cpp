#include "datumloop/engineering/StructuralAnalysis.hpp"

#include "datumloop/core/DatumUuid.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace datumloop {
namespace {

bool hasText(const std::string& value)
{
    return value.find_first_not_of(" \t\n\r") != std::string::npos;
}

bool finiteVector(const Vector3& value)
{
    return std::all_of(value.begin(), value.end(), [](double component) {
        return std::isfinite(component);
    });
}

void requireFiniteNonnegative(double value, const char* field)
{
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(std::string(field) + " must be finite and non-negative");
    }
}

void validateMeshProvenance(const AnalysisMeshProvenance& mesh)
{
    if (!isDatumUuid(mesh.revisionId) || !hasText(mesh.generator) || !hasText(mesh.generatorVersion) ||
        !hasText(mesh.elementType) || !std::isfinite(mesh.requestedMaximumElementSizeM) ||
        mesh.requestedMaximumElementSizeM <= 0.0 || mesh.nodeCount < 8 || mesh.elementCount == 0 ||
        !std::isfinite(mesh.minimumScaledJacobian) || mesh.minimumScaledJacobian <= 0.0 ||
        !std::isfinite(mesh.maximumAspectRatio) || mesh.maximumAspectRatio < 1.0) {
        throw std::invalid_argument("analysis mesh provenance is incomplete or invalid");
    }
}

void validateRunProvenance(const AnalysisRunProvenance& provenance)
{
    if (!isDatumUuid(provenance.solverRunId) || !hasText(provenance.timestampUtc) ||
        !isDatumUuid(provenance.sourceProjectId) || !isDatumUuid(provenance.cadObjectId) ||
        (provenance.geometryFeatureId.has_value() && !isDatumUuid(*provenance.geometryFeatureId)) ||
        !isDatumUuid(provenance.materialDefinitionId) || !hasText(provenance.materialRevision) ||
        (provenance.materialStateId.has_value() && !isDatumUuid(*provenance.materialStateId)) ||
        !hasText(provenance.equationDefinitionId) || !hasText(provenance.solverId) ||
        !hasText(provenance.solverVersion) || !std::isfinite(provenance.absoluteTolerance) ||
        provenance.absoluteTolerance <= 0.0 || !std::isfinite(provenance.relativeTolerance) ||
        provenance.relativeTolerance <= 0.0 || provenance.maximumIterations == 0 ||
        (provenance.agentProposalId.has_value() && !hasText(*provenance.agentProposalId))) {
        throw std::invalid_argument("structural analysis provenance is incomplete or invalid");
    }
    if (provenance.analysisTemperatureK.has_value() &&
        (!std::isfinite(*provenance.analysisTemperatureK) || *provenance.analysisTemperatureK <= 0.0)) {
        throw std::invalid_argument("structural analysis temperature must be finite and positive");
    }
    (void)modelMaturityName(provenance.equationMaturity);
    validateMeshProvenance(provenance.mesh);
    std::unordered_set<std::string> topologyIds;
    for (const std::string& id : provenance.topologyReferenceIds) {
        if (!isDatumUuid(id) || !topologyIds.insert(id).second) {
            throw std::invalid_argument("analysis topology references must be unique UUIDs");
        }
    }
    std::unordered_set<std::string> conditionIds;
    for (const std::string& id : provenance.boundaryConditionIds) {
        if (!isDatumUuid(id) || !conditionIds.insert(id).second) {
            throw std::invalid_argument("analysis boundary condition references must be unique UUIDs");
        }
    }
}

}  // namespace

std::string boundaryConditionId(const BoundaryCondition& condition)
{
    return std::visit([](const auto& value) { return value.id; }, condition);
}

std::string boundaryConditionCadObjectId(const BoundaryCondition& condition)
{
    return std::visit([](const auto& value) { return value.cadObjectId; }, condition);
}

std::optional<std::string> boundaryConditionTopologyReferenceId(const BoundaryCondition& condition)
{
    return std::visit([](const auto& value) -> std::optional<std::string> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, FixedDisplacement> || std::is_same_v<T, AppliedForce> ||
                      std::is_same_v<T, AppliedPressure>) {
            return value.topologyReferenceId;
        }
        return std::nullopt;
    }, condition);
}

void validateBoundaryCondition(const BoundaryCondition& condition)
{
    std::visit([](const auto& value) {
        if (!isDatumUuid(value.id) || !hasText(value.name) || !isDatumUuid(value.cadObjectId)) {
            throw std::invalid_argument("physics condition requires UUID identities and a non-empty name");
        }
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, FixedDisplacement>) {
            if (!isDatumUuid(value.topologyReferenceId) || !finiteVector(value.displacementM) ||
                std::none_of(value.constrainedComponents.begin(), value.constrainedComponents.end(),
                             [](bool constrained) { return constrained; })) {
                throw std::invalid_argument("fixed displacement requires a face reference and finite constrained values");
            }
        } else if constexpr (std::is_same_v<T, AppliedForce>) {
            if (!isDatumUuid(value.topologyReferenceId) || !finiteVector(value.totalForceN) ||
                std::all_of(value.totalForceN.begin(), value.totalForceN.end(),
                            [](double component) { return component == 0.0; })) {
                throw std::invalid_argument("applied force requires a face reference and a non-zero finite force");
            }
        } else if constexpr (std::is_same_v<T, AppliedPressure>) {
            if (!isDatumUuid(value.topologyReferenceId) || !std::isfinite(value.pressurePa) ||
                value.pressurePa == 0.0) {
                throw std::invalid_argument("applied pressure requires a face reference and a non-zero finite pressure");
            }
        } else if constexpr (std::is_same_v<T, BodyForce>) {
            if (!finiteVector(value.forceDensityNPerM3)) {
                throw std::invalid_argument("body force density must contain finite N/m^3 values");
            }
        } else if constexpr (std::is_same_v<T, InitialCondition>) {
            if (!finiteVector(value.displacementM) || !finiteVector(value.velocityMPerS)) {
                throw std::invalid_argument("initial displacement and velocity must be finite");
            }
        }
    }, condition);
}

std::string fieldTypeName(FieldType type)
{
    switch (type) {
    case FieldType::Displacement: return "displacement";
    case FieldType::DisplacementMagnitude: return "displacement_magnitude";
    case FieldType::StrainTensor: return "strain_tensor";
    case FieldType::StressTensor: return "stress_tensor";
    case FieldType::VonMisesStress: return "von_mises_stress";
    }
    throw std::invalid_argument("unknown structural field type");
}

std::optional<FieldType> fieldTypeFromName(const std::string& name)
{
    if (name == "displacement") return FieldType::Displacement;
    if (name == "displacement_magnitude") return FieldType::DisplacementMagnitude;
    if (name == "strain_tensor") return FieldType::StrainTensor;
    if (name == "stress_tensor") return FieldType::StressTensor;
    if (name == "von_mises_stress") return FieldType::VonMisesStress;
    return std::nullopt;
}

std::string fieldLocationName(FieldLocation location)
{
    switch (location) {
    case FieldLocation::Nodes: return "nodes";
    case FieldLocation::Elements: return "elements";
    }
    throw std::invalid_argument("unknown structural field location");
}

std::optional<FieldLocation> fieldLocationFromName(const std::string& name)
{
    if (name == "nodes") return FieldLocation::Nodes;
    if (name == "elements") return FieldLocation::Elements;
    return std::nullopt;
}

void FieldDataset::validate(std::uint64_t nodeCount,
                            std::uint64_t elementCount,
                            const std::string& expectedMeshRevisionId,
                            const std::string& expectedSolverRunId) const
{
    const std::uint64_t sampleCount = location == FieldLocation::Nodes ? nodeCount : elementCount;
    const std::uint64_t componentCount = static_cast<std::uint64_t>(components.size());
    const bool sampleCountOverflow = sampleCount == 0 ||
        componentCount > std::numeric_limits<std::uint64_t>::max() / sampleCount;
    const std::uint64_t expectedValueCount = sampleCountOverflow ? 0 : sampleCount * componentCount;
    if (!isDatumUuid(id) || components.empty() || !hasText(units) ||
        meshRevisionId != expectedMeshRevisionId || solverRunId != expectedSolverRunId ||
        sampleCountOverflow || values.size() != expectedValueCount ||
        !std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); })) {
        throw std::invalid_argument("field dataset metadata or sample values do not match its source mesh and run");
    }
    (void)fieldTypeName(fieldType);
    (void)fieldLocationName(location);
    std::unordered_set<std::string> uniqueComponents;
    for (const std::string& component : components) {
        if (!hasText(component) || !uniqueComponents.insert(component).second) {
            throw std::invalid_argument("field dataset components must be unique non-empty names");
        }
    }
}

void StructuralAnalysisRun::validate(
    const std::vector<std::string>& expectedBoundaryConditionIds) const
{
    validateRunProvenance(provenance);
    if (!converged || iterations == 0 || iterations > provenance.maximumIterations ||
        !std::isfinite(relativeResidual) || relativeResidual < 0.0 ||
        !finiteVector(appliedForceN) || !finiteVector(reactionForceN) ||
        !std::isfinite(relativeForceBalanceError) || relativeForceBalanceError < 0.0 ||
        !std::isfinite(tipDisplacementM) || !std::isfinite(maximumDisplacementM) ||
        maximumDisplacementM < 0.0 || !std::isfinite(maximumVonMisesStressPa) ||
        maximumVonMisesStressPa < 0.0 || !std::isfinite(sectionAxialStressPa) ||
        sectionAxialStressPa < 0.0 || !finiteVector(sectionStressSamplePositionM) ||
        !std::isfinite(strainEnergyJ) || strainEnergyJ < 0.0) {
        throw std::invalid_argument("structural analysis result is not converged or contains invalid metrics");
    }
    if (provenance.boundaryConditionIds != expectedBoundaryConditionIds) {
        throw std::invalid_argument("analysis run provenance does not match the study boundary conditions");
    }
    const auto has = [this](FieldType type, FieldLocation location, std::size_t components,
                            const std::string& units) {
        return std::any_of(fields.begin(), fields.end(), [this, type, location, components, &units](const auto& field) {
            return field.fieldType == type && field.location == location &&
                   field.components.size() == components && field.units == units &&
                   field.meshRevisionId == provenance.mesh.revisionId &&
                   field.solverRunId == provenance.solverRunId;
        });
    };
    if (!has(FieldType::Displacement, FieldLocation::Nodes, 3, "m") ||
        !has(FieldType::StrainTensor, FieldLocation::Elements, 6, "1") ||
        !has(FieldType::StressTensor, FieldLocation::Elements, 6, "Pa") ||
        !has(FieldType::VonMisesStress, FieldLocation::Elements, 1, "Pa")) {
        throw std::invalid_argument("converged structural result must contain displacement, strain, and stress fields");
    }
    std::unordered_set<std::string> fieldIds;
    for (const FieldDataset& field : fields) {
        field.validate(provenance.mesh.nodeCount, provenance.mesh.elementCount,
                       provenance.mesh.revisionId, provenance.solverRunId);
        if (!fieldIds.insert(field.id).second) {
            throw std::invalid_argument("field dataset UUIDs must be unique within a solver run");
        }
    }
}

void StructuralAnalysisStudy::validate() const
{
    if (!isDatumUuid(id) || !hasText(name) || !isDatumUuid(cadObjectId) ||
        !isDatumUuid(materialDefinitionId) || !hasText(equationDefinitionId) ||
        (materialStateId.has_value() && !isDatumUuid(*materialStateId)) ||
        boundaryConditions.empty()) {
        throw std::invalid_argument("structural analysis study requires CAD, material, equation, and physics inputs");
    }
    std::unordered_set<std::string> conditionIds;
    std::vector<std::string> expectedConditionIds;
    for (const BoundaryCondition& condition : boundaryConditions) {
        validateBoundaryCondition(condition);
        if (boundaryConditionCadObjectId(condition) != cadObjectId ||
            !conditionIds.insert(boundaryConditionId(condition)).second) {
            throw std::invalid_argument("study physics conditions must uniquely target its CAD object");
        }
        expectedConditionIds.push_back(boundaryConditionId(condition));
    }
    std::unordered_set<std::string> runIds;
    for (const StructuralAnalysisRun& run : runs) {
        run.validate(expectedConditionIds);
        if (run.provenance.cadObjectId != cadObjectId ||
            run.provenance.materialDefinitionId != materialDefinitionId ||
            run.provenance.materialStateId != materialStateId ||
            run.provenance.equationDefinitionId != equationDefinitionId ||
            !runIds.insert(run.provenance.solverRunId).second) {
            throw std::invalid_argument("study solver runs must use the study's CAD, material, and equation inputs");
        }
    }
    if (meshConvergence.has_value()) {
        const StructuralMeshConvergence& convergence = *meshConvergence;
        if (convergence.solverRunIds.size() < 3 || !std::isfinite(convergence.tolerance) ||
            convergence.tolerance <= 0.0 || convergence.tolerance >= 1.0) {
            throw std::invalid_argument("mesh convergence record requires at least three runs and a valid tolerance");
        }
        std::unordered_set<std::string> convergenceIds;
        for (const std::string& runId : convergence.solverRunIds) {
            if (!runIds.contains(runId) || !convergenceIds.insert(runId).second) {
                throw std::invalid_argument("mesh convergence references must identify unique study runs");
            }
        }
        requireFiniteNonnegative(convergence.relativeTipDisplacementChange,
                                 "relative tip displacement change");
        requireFiniteNonnegative(convergence.relativeStrainEnergyChange,
                                 "relative strain energy change");
        requireFiniteNonnegative(convergence.relativeSectionStressChange,
                                 "relative section stress change");
        const bool metricsConverged =
            convergence.relativeTipDisplacementChange <= convergence.tolerance &&
            convergence.relativeStrainEnergyChange <= convergence.tolerance &&
            convergence.relativeSectionStressChange <= convergence.tolerance;
        if (convergence.converged != metricsConverged) {
            throw std::invalid_argument("mesh convergence status does not agree with its recorded metrics");
        }
    }
}

}  // namespace datumloop
